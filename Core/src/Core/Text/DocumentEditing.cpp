#include "corepch.h"

#include "Core/Text/DocumentEditing.h"

#include "Core/Base/Ascii.h"
#include "Core/Text/Caret.h"
#include "Core/Text/TextDocument.h"

#include <optional>

namespace Core {

	namespace {
		constexpr char kSpaceAfterCommentPrefix = ' ';
		constexpr std::size_t kTabByteCount = 1;
		constexpr std::size_t kSpaceAfterCommentPrefixByteCount = 1;

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

		Caret SelectionToDelete(const TextBuffer& buffer, const Caret& caret, std::size_t tabWidth,
								Motion motion)
		{
			if (HasSelection(caret))
			{
				return caret;
			}

			const std::size_t noPageLines = 0;

			return Move(buffer, caret, motion, SelectionChange::Extend, tabWidth, noPageLines);
		}

		bool DeleteSelectionOrMotion(TextDocument& document, Caret& caret, std::size_t tabWidth,
									 Motion motion)
		{
			if (document.IsReadOnly())
			{
				return false;
			}

			TextBuffer& buffer = document.GetBuffer();

			const Caret selectionToDelete = SelectionToDelete(buffer, caret, tabWidth, motion);

			const auto [deleteFrom, deleteTo] = SelectionRange(selectionToDelete);

			if (deleteFrom == deleteTo)
			{
				return false;
			}

			buffer.BeginTransaction();
			buffer.Erase(deleteFrom, deleteTo);
			buffer.EndTransaction();

			CollapseCaretTo(caret, deleteFrom);

			return true;
		}

		void SettleAfterHistoryStep(TextDocument& document, Caret& caret, std::size_t caretOffset)
		{
			TextBuffer& buffer = document.GetBuffer();

			buffer.BreakUndoGroup();

			CollapseCaretTo(caret, std::min(caretOffset, buffer.GetLength()));
		}

		std::string LeadingWhitespace(std::string_view lineText)
		{
			const std::size_t indentEnd = lineText.find_first_not_of(" \t");

			return std::string{lineText.substr(0, indentEnd == std::string_view::npos ? lineText.size() : indentEnd)};
		}

		bool InsertAsOwnUndoGroup(TextDocument& document, Caret& caret, TextBuffer& buffer,
								  std::string_view text)
		{
			buffer.BreakUndoGroup();
			const bool changed = InsertText(document, caret, text);
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

	bool InsertText(TextDocument& document, Caret& caret, std::string_view utf8)
	{
		if (document.IsReadOnly() || utf8.empty())
		{
			return false;
		}

		const std::string withNewlines = WithEveryLineEndingAsNewline(utf8);
		TextBuffer& buffer = document.GetBuffer();
		const auto [selectionStart, selectionEnd] = SelectionRange(caret);

		buffer.BeginTransaction();

		if (selectionStart != selectionEnd)
		{
			buffer.Erase(selectionStart, selectionEnd);
		}

		buffer.Insert(selectionStart, withNewlines);
		buffer.EndTransaction();

		CollapseCaretTo(caret, selectionStart + withNewlines.size());

		return true;
	}

	bool DeleteBackward(TextDocument& document, Caret& caret, std::size_t tabWidth)
	{
		return DeleteSelectionOrMotion(document, caret, tabWidth, Motion::Left);
	}

	bool DeleteForward(TextDocument& document, Caret& caret, std::size_t tabWidth)
	{
		return DeleteSelectionOrMotion(document, caret, tabWidth, Motion::Right);
	}

	bool DeleteWordBackward(TextDocument& document, Caret& caret, std::size_t tabWidth)
	{
		return DeleteSelectionOrMotion(document, caret, tabWidth, Motion::WordLeft);
	}

	bool DeleteWordForward(TextDocument& document, Caret& caret, std::size_t tabWidth)
	{
		return DeleteSelectionOrMotion(document, caret, tabWidth, Motion::WordRight);
	}

	// Stepping through history is not gated on IsReadOnly: every state in the history is one the
	// buffer already held, so a reload that turned the document read-only stays reversible.
	bool Undo(TextDocument& document, Caret& caret)
	{
		const std::optional<std::size_t> caretOffset = document.GetBuffer().Undo();

		if (!caretOffset)
		{
			return false;
		}

		SettleAfterHistoryStep(document, caret, *caretOffset);

		return true;
	}

	bool Redo(TextDocument& document, Caret& caret)
	{
		const std::optional<std::size_t> caretOffset = document.GetBuffer().Redo();

		if (!caretOffset)
		{
			return false;
		}

		SettleAfterHistoryStep(document, caret, *caretOffset);

		return true;
	}

	// The new line copies the current line's indentation instead of building one from the indent setting,
	// so a tab-indented file stays tab-indented under a spaces setting.
	bool InsertNewline(TextDocument& document, Caret& caret)
	{
		if (document.IsReadOnly())
		{
			return false;
		}

		TextBuffer& buffer = document.GetBuffer();
		const auto [selectionStart, selectionEnd] = SelectionRange(caret);
		const std::size_t selectionStartLine = buffer.ToPosition(selectionStart).Line;

		const std::string newlineWithIndent = "\n" + LeadingWhitespace(buffer.LineText(selectionStartLine));

		buffer.BeginTransaction();

		if (selectionStart != selectionEnd)
		{
			buffer.Erase(selectionStart, selectionEnd);
		}

		buffer.Insert(selectionStart, newlineWithIndent);
		buffer.EndTransaction();

		CollapseCaretTo(caret, selectionStart + newlineWithIndent.size());

		return true;
	}

	bool Indent(TextDocument& document, Caret& caret, IndentStyle style)
	{
		if (document.IsReadOnly())
		{
			return false;
		}

		const std::string indentUnit =
			style.UseSpaces ? std::string(style.Width, ' ') : std::string{"\t"};

		TextBuffer& buffer = document.GetBuffer();

		if (!HasSelection(caret))
		{
			// Refused before the undo group is broken: InsertText also turns empty text down, but
			// only after the break has already split the surrounding typing in two.
			if (indentUnit.empty())
			{
				return false;
			}

			return InsertAsOwnUndoGroup(document, caret, buffer, indentUnit);
		}

		const auto [firstLine, lastLine] = LinesTouchedBySelection(buffer, caret);

		buffer.BreakUndoGroup();
		buffer.BeginTransaction();

		for (std::size_t linesLeft = lastLine - firstLine + 1; linesLeft > 0; --linesLeft)
		{
			const std::size_t line = firstLine + linesLeft - 1;

			buffer.Insert(buffer.LineStart(line), indentUnit);
		}

		buffer.EndTransaction();
		buffer.BreakUndoGroup();

		SelectWholeLines(caret, buffer, firstLine, lastLine);

		return true;
	}

	bool Outdent(TextDocument& document, Caret& caret, IndentStyle style)
	{
		if (document.IsReadOnly())
		{
			return false;
		}

		TextBuffer& buffer = document.GetBuffer();
		const auto [firstLine, lastLine] = LinesTouchedBySelection(buffer, caret);

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

		SelectWholeLines(caret, buffer, firstLine, lastLine);

		return true;
	}

	bool ToggleLineComment(TextDocument& document, Caret& caret, std::string_view commentPrefix)
	{
		if (document.IsReadOnly() || commentPrefix.empty())
		{
			return false;
		}

		TextBuffer& buffer = document.GetBuffer();
		const bool hadSelection = HasSelection(caret);
		const auto [firstLine, lastLine] = LinesTouchedBySelection(buffer, caret);

		const bool uncommentLines = EveryNonBlankLineIsCommented(buffer, firstLine, lastLine, commentPrefix);

		const std::size_t caretLine = buffer.ToPosition(caret.Offset).Line;
		const std::size_t caretLineLengthBeforeEdit = buffer.LineLength(caretLine);
		const std::size_t caretLineStartBeforeEdit = buffer.LineStart(caretLine);
		const std::size_t caretColumnBeforeEdit = caret.Offset - caretLineStartBeforeEdit;

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
			SelectWholeLines(caret, buffer, firstLine, lastLine);
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

			CollapseCaretTo(caret,
							buffer.LineStart(caretLine) + std::min(caretColumnAfterEdit, caretLineLengthAfterEdit));
		}

		return true;
	}
}
