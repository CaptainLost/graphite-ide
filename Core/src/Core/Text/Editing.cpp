#include "corepch.h"

#include "Core/Text/Editing.h"

#include "Core/Base/Ascii.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Indent.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextDocument.h"

namespace Core {

	namespace {
		constexpr char kSpaceAfterCommentPrefix = ' ';
		constexpr std::size_t kTabByteCount = 1;
		constexpr std::size_t kSpaceAfterCommentPrefixByteCount = 1;
		constexpr std::size_t kPlaceholderIndentDepth = 0;

		struct LineBlock
		{
			std::size_t FirstLine = 0;
			std::size_t LastLine = 0;
		};

		struct PlannedRemoval
		{
			std::size_t Offset = 0;
			std::size_t ByteCount = 0;
		};

		bool IsEditable(const OpenItem& item)
		{
			return item.Document && !item.Document->IsReadOnly();
		}

		// Not IsEditable: every state in the history is one the buffer already held, so a reload
		// that turned the document read-only stays reversible.
		bool CanStepThroughHistory(const OpenItem& item)
		{
			return item.Document.has_value();
		}

		std::string WithEveryLineEndingAsNewline(std::string_view text)
		{
			std::string withNewlines;
			withNewlines.reserve(text.size());

			for (std::size_t byteIndex = 0; byteIndex < text.size(); ++byteIndex)
			{
				if (text[byteIndex] == '\r')
				{
					const bool isCrLfPair = byteIndex + 1 < text.size() && text[byteIndex + 1] == '\n';

					if (isCrLfPair)
					{
						++byteIndex;
					}

					withNewlines.push_back('\n');
					continue;
				}

				withNewlines.push_back(text[byteIndex]);
			}

			return withNewlines;
		}

		void CollapseCaretTo(Caret& caret, std::size_t offset)
		{
			caret.Offset = offset;
			caret.Anchor = offset;
			caret.DesiredColumn = NoDesiredColumn;
		}

		void SelectWholeLines(Caret& caret, const TextBuffer& buffer, std::size_t firstLine,
							  std::size_t lastLine)
		{
			caret.Anchor = buffer.LineStart(firstLine);
			caret.Offset = buffer.LineStart(lastLine) + buffer.LineLength(lastLine);
			caret.DesiredColumn = NoDesiredColumn;
		}

		bool IsBlankLine(const TextBuffer& buffer, std::size_t line)
		{
			return ContainsOnlySpacesAndTabs(buffer.LineText(line));
		}

		LineBlock WidenOverBlankNeighbours(const TextBuffer& buffer, std::size_t firstLine,
										   std::size_t lastLine)
		{
			const std::size_t lastLineOfBuffer = buffer.GetLineCount() - 1;
			std::size_t widenedFirst = std::min(firstLine, lastLineOfBuffer);

			while (widenedFirst > 0 && IsBlankLine(buffer, widenedFirst))
			{
				--widenedFirst;
			}

			std::size_t widenedLast = std::min(lastLine, lastLineOfBuffer);

			while (widenedLast < lastLineOfBuffer && IsBlankLine(buffer, widenedLast))
			{
				++widenedLast;
			}

			return LineBlock{.FirstLine = widenedFirst, .LastLine = widenedLast};
		}

		// resize() here would pad or truncate only the tail, pairing every surviving depth below the edit with the wrong line.
		void SpliceDepthsAtEditSite(OpenItem& item, std::size_t firstLine, std::size_t currentLineCount)
		{
			const std::size_t previousLineCount = item.IndentDepths.size();
			const std::size_t editSite = std::min(firstLine, previousLineCount);

			if (currentLineCount > previousLineCount)
			{
				item.IndentDepths.insert(
					item.IndentDepths.begin() + static_cast<std::ptrdiff_t>(editSite),
					currentLineCount - previousLineCount, kPlaceholderIndentDepth);
			}
			else if (currentLineCount < previousLineCount)
			{
				const std::size_t spliceEnd =
					std::min(editSite + (previousLineCount - currentLineCount), previousLineCount);

				item.IndentDepths.erase(
					item.IndentDepths.begin() + static_cast<std::ptrdiff_t>(editSite),
					item.IndentDepths.begin() + static_cast<std::ptrdiff_t>(spliceEnd));
			}
		}

		void RecomputeDepths(OpenItem& item, const TextBuffer& buffer, std::size_t firstLine,
							 std::size_t lastLine)
		{
			const std::vector<std::size_t> depths = IndentDepths(buffer, item.TabWidth, firstLine, lastLine);

			for (std::size_t line = firstLine; line <= lastLine; ++line)
			{
				item.IndentDepths[line] = depths[line - firstLine];
			}
		}

		void UpdateIndentDepths(OpenItem& item, std::size_t firstLine, std::size_t lastLine)
		{
			const TextBuffer& buffer = item.Document->GetBuffer();

			SpliceDepthsAtEditSite(item, firstLine, buffer.GetLineCount());

			const LineBlock widened = WidenOverBlankNeighbours(buffer, firstLine, lastLine);

			RecomputeDepths(item, buffer, widened.FirstLine, widened.LastLine);
		}

		void SpliceRowsAtEditSite(OpenItem& item, const TextBuffer& buffer, std::size_t firstLine)
		{
			const std::size_t previousLineCount = item.Rows.GetLineCount();
			const std::size_t currentLineCount = buffer.GetLineCount();
			const std::size_t editSite = std::min(firstLine, previousLineCount);

			if (currentLineCount > previousLineCount)
			{
				item.Rows.InsertLines(buffer, editSite, currentLineCount - previousLineCount);
			}
			else if (currentLineCount < previousLineCount)
			{
				item.Rows.RemoveLines(editSite, previousLineCount - currentLineCount);
			}
		}

		void RecordLineIfWidest(OpenItem& item, const TextBuffer& buffer, std::size_t line)
		{
			const std::size_t width = ColumnCount(buffer.LineText(line), item.TabWidth);

			if (width > item.WidestColumn)
			{
				item.WidestColumn = width;
				item.WidestLine = line;
			}
		}

		void ExtendWidestLine(OpenItem& item, const TextBuffer& buffer, std::size_t firstLine,
							  std::size_t lastLine)
		{
			for (std::size_t line = firstLine; line <= lastLine && line < buffer.GetLineCount();
				 ++line)
			{
				RecordLineIfWidest(item, buffer, line);
			}
		}

		void UpdateLineCachesAfterEdit(OpenItem& item, std::size_t firstLine, std::size_t lastLine)
		{
			const TextBuffer& buffer = item.Document->GetBuffer();

			const bool widestLineNoLongerExists = item.WidestLine >= buffer.GetLineCount();
			const bool editMayHaveMovedOrShortenedWidestLine = item.WidestLine >= firstLine;

			if (widestLineNoLongerExists || editMayHaveMovedOrShortenedWidestLine)
			{
				RecomputeLineCaches(item);
				return;
			}

			ExtendWidestLine(item, buffer, firstLine, lastLine);
			UpdateIndentDepths(item, firstLine, lastLine);
			SpliceRowsAtEditSite(item, buffer, firstLine);

			for (std::size_t line = firstLine; line <= lastLine && line < buffer.GetLineCount();
				 ++line)
			{
				item.Rows.UpdateLine(buffer, line);
			}
		}

		LineBlock LinesTouchedBySelection(const TextBuffer& buffer, const Caret& caret)
		{
			const auto [selectionStart, selectionEnd] = SelectionRange(caret);

			const std::size_t firstLine = buffer.ToPosition(selectionStart).Line;
			std::size_t lastLine = buffer.ToPosition(selectionEnd).Line;

			const bool selectionSpansLines = lastLine > firstLine;
			const bool selectionEndsAtStartOfLastLine = selectionEnd == buffer.LineStart(lastLine);

			if (selectionSpansLines && selectionEndsAtStartOfLastLine)
			{
				--lastLine;
			}

			return LineBlock{.FirstLine = firstLine, .LastLine = lastLine};
		}

		Caret SelectionToDelete(const TextBuffer& buffer, const OpenItem& item, Motion motion)
		{
			if (HasSelection(item.Caret))
			{
				return item.Caret;
			}

			const std::size_t noPageLines = 0;

			return Move(buffer, item.Caret, motion, SelectionChange::Extend, item.TabWidth, noPageLines);
		}

		bool DeleteSelectionOrMotion(OpenItem& item, Motion motion)
		{
			if (!IsEditable(item))
			{
				return false;
			}

			TextBuffer& buffer = item.Document->GetBuffer();

			const Caret selectionToDelete = SelectionToDelete(buffer, item, motion);

			const auto [deleteFrom, deleteTo] = SelectionRange(selectionToDelete);

			if (deleteFrom == deleteTo)
			{
				return false;
			}

			const std::size_t firstLine = buffer.ToPosition(deleteFrom).Line;

			buffer.BeginTransaction();
			buffer.Erase(deleteFrom, deleteTo);
			buffer.EndTransaction();

			CollapseCaretTo(item.Caret, deleteFrom);

			UpdateLineCachesAfterEdit(item, firstLine, firstLine);

			return true;
		}

		void SettleAfterHistoryStep(OpenItem& item, std::size_t caretOffset)
		{
			TextBuffer& buffer = item.Document->GetBuffer();

			buffer.BreakUndoGroup();

			CollapseCaretTo(item.Caret, std::min(caretOffset, buffer.GetLength()));

			RecomputeLineCaches(item);
		}

		std::string LeadingWhitespace(std::string_view lineText)
		{
			const std::size_t indentEnd = lineText.find_first_not_of(" \t");

			return std::string{lineText.substr(0, indentEnd == std::string_view::npos ? lineText.size() : indentEnd)};
		}

		bool InsertAsOwnUndoGroup(OpenItem& item, TextBuffer& buffer, std::string_view text)
		{
			buffer.BreakUndoGroup();
			const bool changed = InsertText(item, text);
			buffer.BreakUndoGroup();

			return changed;
		}

		bool IsSpaceAt(std::string_view text, std::size_t byteIndex)
		{
			return byteIndex < text.size() && text[byteIndex] == ' ';
		}

		std::size_t OutdentBytesOf(std::string_view lineText, std::size_t spaceWidth)
		{
			if (lineText.starts_with('\t'))
			{
				return kTabByteCount;
			}

			std::size_t spaces = 0;

			while (spaces < spaceWidth && IsSpaceAt(lineText, spaces))
			{
				++spaces;
			}

			return spaces;
		}

		std::vector<PlannedRemoval> PlanOutdent(const TextBuffer& buffer, std::size_t firstLine,
												std::size_t lastLine, std::size_t spaceWidth)
		{
			std::vector<PlannedRemoval> removals;

			for (std::size_t line = firstLine; line <= lastLine; ++line)
			{
				const std::size_t outdentBytes = OutdentBytesOf(buffer.LineText(line), spaceWidth);

				if (outdentBytes > 0)
				{
					removals.push_back(PlannedRemoval{.Offset = buffer.LineStart(line), .ByteCount = outdentBytes});
				}
			}

			return removals;
		}

		// Every offset was planned on the text before any erase, so erasing from the front would
		// shift the text under every later removal.
		void EraseBackToFront(TextBuffer& buffer, const std::vector<PlannedRemoval>& removals)
		{
			for (std::size_t removalsLeft = removals.size(); removalsLeft > 0; --removalsLeft)
			{
				const PlannedRemoval& removal = removals[removalsLeft - 1];

				buffer.Erase(removal.Offset, removal.Offset + removal.ByteCount);
			}
		}

		std::size_t ColumnOfLastRemovalOnLine(const std::vector<PlannedRemoval>& removals,
											  std::size_t lineStart, std::size_t lineLength)
		{
			std::size_t column = 0;

			for (const PlannedRemoval& removal : removals)
			{
				const bool removalIsOnLine = removal.Offset >= lineStart && removal.Offset <= lineStart + lineLength;

				if (removalIsOnLine)
				{
					column = removal.Offset - lineStart;
				}
			}

			return column;
		}

		std::size_t CommonPrefixLength(std::string_view left, std::string_view right)
		{
			const std::size_t shorterLength = std::min(left.size(), right.size());
			std::size_t length = 0;

			while (length < shorterLength && left[length] == right[length])
			{
				++length;
			}

			return length;
		}

		// Byte for byte rather than by width: a tab and four spaces are equally wide but are not
		// the same text, and a prefix inserted at a shared width would split one of them.
		std::size_t IndentBytesSharedByNonBlankLines(const TextBuffer& buffer, std::size_t firstLine,
													 std::size_t lastLine)
		{
			std::optional<std::string> sharedIndent;

			for (std::size_t line = firstLine; line <= lastLine; ++line)
			{
				if (IsBlankLine(buffer, line))
				{
					continue;
				}

				const std::string lineIndent = LeadingWhitespace(buffer.LineText(line));

				if (!sharedIndent)
				{
					sharedIndent = lineIndent;
					continue;
				}

				sharedIndent->resize(CommonPrefixLength(*sharedIndent, lineIndent));
			}

			return sharedIndent ? sharedIndent->size() : 0;
		}

		bool EveryNonBlankLineIsCommented(const TextBuffer& buffer, std::size_t firstLine,
										  std::size_t lastLine, std::string_view commentPrefix)
		{
			bool foundNonBlankLine = false;

			for (std::size_t line = firstLine; line <= lastLine; ++line)
			{
				if (IsBlankLine(buffer, line))
				{
					continue;
				}

				const std::string lineText = buffer.LineText(line);
				const std::size_t indentBytes = LeadingWhitespace(lineText).size();
				const bool isCommented = std::string_view{lineText}.substr(indentBytes).starts_with(commentPrefix);

				if (!isCommented)
				{
					return false;
				}

				foundNonBlankLine = true;
			}

			return foundNonBlankLine;
		}

		std::vector<PlannedRemoval> PlanUncomment(const TextBuffer& buffer, std::size_t firstLine,
												  std::size_t lastLine, std::string_view commentPrefix)
		{
			std::vector<PlannedRemoval> removals;

			for (std::size_t line = firstLine; line <= lastLine; ++line)
			{
				if (IsBlankLine(buffer, line))
				{
					continue;
				}

				const std::string lineText = buffer.LineText(line);
				const std::size_t indentBytes = LeadingWhitespace(lineText).size();
				const std::size_t afterPrefix = indentBytes + commentPrefix.size();
				const bool spaceFollowsPrefix = afterPrefix < lineText.size() && lineText[afterPrefix] == kSpaceAfterCommentPrefix;
				const std::size_t spaceBytes = spaceFollowsPrefix ? kSpaceAfterCommentPrefixByteCount : 0;

				removals.push_back(PlannedRemoval{.Offset = buffer.LineStart(line) + indentBytes,
												  .ByteCount = commentPrefix.size() + spaceBytes});
			}

			return removals;
		}
	}

	void RecomputeLineCaches(OpenItem& item)
	{
		const TextBuffer& buffer = item.Document->GetBuffer();

		item.IndentDepths = IndentDepths(buffer, item.TabWidth);
		item.WidestColumn = 0;
		item.WidestLine = 0;

		for (std::size_t line = 0; line < buffer.GetLineCount(); ++line)
		{
			RecordLineIfWidest(item, buffer, line);
		}

		// Rebuilt at the width already in force, not at kNoWrap: the editor rewraps only when its
		// width changes, so a document on screen would stay unwrapped until the next resize.
		const std::size_t columnsPerRowInForce = item.Rows.GetColumnsPerRow();
		item.Rows.Rebuild(buffer, columnsPerRowInForce, item.TabWidth);
	}

	bool InsertText(OpenItem& item, std::string_view utf8)
	{
		if (!IsEditable(item) || utf8.empty())
		{
			return false;
		}

		const std::string withNewlines = WithEveryLineEndingAsNewline(utf8);
		TextBuffer& buffer = item.Document->GetBuffer();
		const auto [selectionStart, selectionEnd] = SelectionRange(item.Caret);

		const std::size_t firstLine = buffer.ToPosition(selectionStart).Line;

		buffer.BeginTransaction();

		if (selectionStart != selectionEnd)
		{
			buffer.Erase(selectionStart, selectionEnd);
		}

		buffer.Insert(selectionStart, withNewlines);
		buffer.EndTransaction();

		CollapseCaretTo(item.Caret, selectionStart + withNewlines.size());

		const std::size_t caretLine = buffer.ToPosition(item.Caret.Offset).Line;
		UpdateLineCachesAfterEdit(item, firstLine, caretLine);

		return true;
	}

	bool DeleteBackward(OpenItem& item)
	{
		return DeleteSelectionOrMotion(item, Motion::Left);
	}

	bool DeleteForward(OpenItem& item)
	{
		return DeleteSelectionOrMotion(item, Motion::Right);
	}

	bool DeleteWordBackward(OpenItem& item)
	{
		return DeleteSelectionOrMotion(item, Motion::WordLeft);
	}

	bool DeleteWordForward(OpenItem& item)
	{
		return DeleteSelectionOrMotion(item, Motion::WordRight);
	}

	bool Undo(OpenItem& item)
	{
		if (!CanStepThroughHistory(item))
		{
			return false;
		}

		const std::optional<std::size_t> caretOffset = item.Document->GetBuffer().Undo();

		if (!caretOffset)
		{
			return false;
		}

		SettleAfterHistoryStep(item, *caretOffset);

		return true;
	}

	bool Redo(OpenItem& item)
	{
		if (!CanStepThroughHistory(item))
		{
			return false;
		}

		const std::optional<std::size_t> caretOffset = item.Document->GetBuffer().Redo();

		if (!caretOffset)
		{
			return false;
		}

		SettleAfterHistoryStep(item, *caretOffset);

		return true;
	}

	// The new line copies the current line's indentation instead of building one from the indent setting,
	// so a tab-indented file stays tab-indented under a spaces setting.
	bool InsertNewline(OpenItem& item)
	{
		if (!IsEditable(item))
		{
			return false;
		}

		TextBuffer& buffer = item.Document->GetBuffer();
		const auto [selectionStart, selectionEnd] = SelectionRange(item.Caret);
		const std::size_t selectionStartLine = buffer.ToPosition(selectionStart).Line;

		const std::string newlineWithIndent = "\n" + LeadingWhitespace(buffer.LineText(selectionStartLine));

		buffer.BeginTransaction();

		if (selectionStart != selectionEnd)
		{
			buffer.Erase(selectionStart, selectionEnd);
		}

		buffer.Insert(selectionStart, newlineWithIndent);
		buffer.EndTransaction();

		CollapseCaretTo(item.Caret, selectionStart + newlineWithIndent.size());

		const std::size_t caretLine = buffer.ToPosition(item.Caret.Offset).Line;
		UpdateLineCachesAfterEdit(item, selectionStartLine, caretLine);

		return true;
	}

	bool Indent(OpenItem& item, IndentStyle style)
	{
		if (!IsEditable(item))
		{
			return false;
		}

		const std::string indentUnit =
			style.UseSpaces ? std::string(style.Width, ' ') : std::string{"\t"};

		TextBuffer& buffer = item.Document->GetBuffer();

		if (!HasSelection(item.Caret))
		{
			// Refused before the undo group is broken: InsertText also turns empty text down, but
			// only after the break has already split the surrounding typing in two.
			if (indentUnit.empty())
			{
				return false;
			}

			return InsertAsOwnUndoGroup(item, buffer, indentUnit);
		}

		const auto [firstLine, lastLine] = LinesTouchedBySelection(buffer, item.Caret);

		buffer.BreakUndoGroup();
		buffer.BeginTransaction();

		for (std::size_t linesLeft = lastLine - firstLine + 1; linesLeft > 0; --linesLeft)
		{
			const std::size_t line = firstLine + linesLeft - 1;

			buffer.Insert(buffer.LineStart(line), indentUnit);
		}

		buffer.EndTransaction();
		buffer.BreakUndoGroup();

		SelectWholeLines(item.Caret, buffer, firstLine, lastLine);

		UpdateLineCachesAfterEdit(item, firstLine, lastLine);

		return true;
	}

	bool Outdent(OpenItem& item, IndentStyle style)
	{
		if (!IsEditable(item))
		{
			return false;
		}

		TextBuffer& buffer = item.Document->GetBuffer();
		const auto [firstLine, lastLine] = LinesTouchedBySelection(buffer, item.Caret);

		const std::vector<PlannedRemoval> removals =
			PlanOutdent(buffer, firstLine, lastLine, style.Width);

		if (removals.empty())
		{
			return false;
		}

		buffer.BreakUndoGroup();
		buffer.BeginTransaction();

		EraseBackToFront(buffer, removals);

		buffer.EndTransaction();
		buffer.BreakUndoGroup();

		SelectWholeLines(item.Caret, buffer, firstLine, lastLine);

		UpdateLineCachesAfterEdit(item, firstLine, lastLine);

		return true;
	}

	bool ToggleLineComment(OpenItem& item, std::string_view commentPrefix)
	{
		if (!IsEditable(item) || commentPrefix.empty())
		{
			return false;
		}

		TextBuffer& buffer = item.Document->GetBuffer();
		const bool hadSelection = HasSelection(item.Caret);
		const auto [firstLine, lastLine] = LinesTouchedBySelection(buffer, item.Caret);

		const bool uncommentLines = EveryNonBlankLineIsCommented(buffer, firstLine, lastLine, commentPrefix);

		const std::size_t caretLine = buffer.ToPosition(item.Caret.Offset).Line;
		const std::size_t caretLineLengthBeforeEdit = buffer.LineLength(caretLine);
		const std::size_t caretLineStartBeforeEdit = buffer.LineStart(caretLine);
		const std::size_t caretColumnBeforeEdit = item.Caret.Offset - caretLineStartBeforeEdit;

		buffer.BreakUndoGroup();
		buffer.BeginTransaction();

		bool changed = false;
		std::size_t editColumnOnCaretLine = 0;

		if (uncommentLines)
		{
			const std::vector<PlannedRemoval> removals =
				PlanUncomment(buffer, firstLine, lastLine, commentPrefix);

			editColumnOnCaretLine =
				ColumnOfLastRemovalOnLine(removals, caretLineStartBeforeEdit, caretLineLengthBeforeEdit);

			EraseBackToFront(buffer, removals);
			changed = !removals.empty();
		}
		else
		{
			const std::size_t sharedIndentBytes = IndentBytesSharedByNonBlankLines(buffer, firstLine, lastLine);
			editColumnOnCaretLine = sharedIndentBytes;
			const std::string insertedPrefix = std::string{commentPrefix} + kSpaceAfterCommentPrefix;

			for (std::size_t linesLeft = lastLine - firstLine + 1; linesLeft > 0; --linesLeft)
			{
				const std::size_t line = firstLine + linesLeft - 1;

				if (IsBlankLine(buffer, line))
				{
					continue;
				}

				buffer.Insert(buffer.LineStart(line) + sharedIndentBytes, insertedPrefix);
				changed = true;
			}
		}

		buffer.EndTransaction();
		buffer.BreakUndoGroup();

		if (!changed)
		{
			return false;
		}

		if (hadSelection)
		{
			SelectWholeLines(item.Caret, buffer, firstLine, lastLine);
		}
		else
		{
			const std::size_t caretLineLengthAfterEdit = buffer.LineLength(caretLine);
			const bool caretIsAtOrAfterEdit = caretColumnBeforeEdit >= editColumnOnCaretLine;
			std::size_t caretColumnAfterEdit = caretColumnBeforeEdit;

			if (caretIsAtOrAfterEdit)
			{
				const bool lineDidNotShrink = caretLineLengthAfterEdit >= caretLineLengthBeforeEdit;

				if (lineDidNotShrink)
				{
					caretColumnAfterEdit = caretColumnBeforeEdit + (caretLineLengthAfterEdit - caretLineLengthBeforeEdit);
				}
				else
				{
					const std::size_t bytesRemoved = caretLineLengthBeforeEdit - caretLineLengthAfterEdit;
					const bool caretWasInsideRemovedText = caretColumnBeforeEdit < editColumnOnCaretLine + bytesRemoved;

					caretColumnAfterEdit = caretWasInsideRemovedText ? editColumnOnCaretLine : caretColumnBeforeEdit - bytesRemoved;
				}
			}

			CollapseCaretTo(item.Caret,
							buffer.LineStart(caretLine) + std::min(caretColumnAfterEdit, caretLineLengthAfterEdit));
		}

		UpdateLineCachesAfterEdit(item, firstLine, lastLine);

		return true;
	}
}
