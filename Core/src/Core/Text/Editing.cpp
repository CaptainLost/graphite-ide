#include "corepch.h"

#include "Core/Text/Editing.h"

#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Indent.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextDocument.h"

namespace Core {

	namespace {
		bool Editable(const OpenItem& item)
		{
			return item.Document && !item.Document->IsReadOnly();
		}

		std::string NormaliseNewlines(std::string_view text)
		{
			std::string result;
			result.reserve(text.size());

			for (std::size_t index = 0; index < text.size(); ++index)
			{
				if (text[index] == '\r')
				{
					const bool isCrLfPair = index + 1 < text.size() && text[index + 1] == '\n';

					if (isCrLfPair)
						++index;

					result.push_back('\n');
					continue;
				}

				result.push_back(text[index]);
			}

			return result;
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
			return buffer.LineText(line).find_first_not_of(" \t") == std::string::npos;
		}

		std::pair<std::size_t, std::size_t> WidenOverBlankNeighbours(const TextBuffer& buffer,
																	 std::size_t firstLine,
																	 std::size_t lastLine)
		{
			std::size_t from = std::min(firstLine, buffer.GetLineCount() - 1);

			while (from > 0 && IsBlankLine(buffer, from))
				--from;

			std::size_t to = std::min(lastLine, buffer.GetLineCount() - 1);

			while (to + 1 < buffer.GetLineCount() && IsBlankLine(buffer, to))
				++to;

			return {from, to};
		}

		void SpliceDepthsAtEditSite(OpenItem& item, std::size_t firstLine, std::size_t currentCount)
		{
			// resize() only pads or truncates the tail, which pairs every surviving depth
			// below the edit with the wrong line.
			const std::size_t previousCount = item.IndentDepths.size();
			const std::size_t at = std::min(firstLine, previousCount);

			if (currentCount > previousCount)
			{
				item.IndentDepths.insert(
					item.IndentDepths.begin() + static_cast<std::ptrdiff_t>(at),
					currentCount - previousCount, 0);
			}
			else if (currentCount < previousCount)
			{
				const std::size_t end = std::min(at + (previousCount - currentCount), previousCount);

				item.IndentDepths.erase(
					item.IndentDepths.begin() + static_cast<std::ptrdiff_t>(at),
					item.IndentDepths.begin() + static_cast<std::ptrdiff_t>(end));
			}
		}

		void RecomputeDepths(OpenItem& item, const TextBuffer& buffer, std::size_t from,
							 std::size_t to)
		{
			const std::vector<std::size_t> depths = IndentDepths(buffer, item.TabWidth, from, to);

			for (std::size_t line = from; line <= to; ++line)
				item.IndentDepths[line] = depths[line - from];
		}

		void UpdateIndentDepths(OpenItem& item, std::size_t firstLine, std::size_t lastLine)
		{
			const TextBuffer& buffer = item.Document->GetBuffer();

			SpliceDepthsAtEditSite(item, firstLine, buffer.GetLineCount());

			const auto [from, to] = WidenOverBlankNeighbours(buffer, firstLine, lastLine);

			RecomputeDepths(item, buffer, from, to);
		}

		void SpliceRowsAtEditSite(OpenItem& item, const TextBuffer& buffer, std::size_t firstLine)
		{
			const std::size_t previousLines = item.Rows.GetLineCount();
			const std::size_t currentLines = buffer.GetLineCount();
			const std::size_t at = std::min(firstLine, previousLines);

			if (currentLines > previousLines)
				item.Rows.InsertLines(buffer, at, currentLines - previousLines);
			else if (currentLines < previousLines)
				item.Rows.RemoveLines(at, previousLines - currentLines);
		}

		void ExtendWidestLine(OpenItem& item, const TextBuffer& buffer, std::size_t firstLine,
							  std::size_t lastLine)
		{
			for (std::size_t line = firstLine; line <= lastLine && line < buffer.GetLineCount();
				 ++line)
			{
				const std::size_t width = ColumnCount(buffer.LineText(line), item.TabWidth);

				if (width > item.WidestColumn)
				{
					item.WidestColumn = width;
					item.WidestLine = line;
				}
			}
		}

		void UpdateDerived(OpenItem& item, std::size_t firstLine, std::size_t lastLine)
		{
			const TextBuffer& buffer = item.Document->GetBuffer();

			const bool widestLineMayHaveShrunk =
				item.WidestLine >= buffer.GetLineCount() || item.WidestLine >= firstLine;

			if (widestLineMayHaveShrunk)
			{
				RefreshDerived(item);
				return;
			}

			ExtendWidestLine(item, buffer, firstLine, lastLine);
			UpdateIndentDepths(item, firstLine, lastLine);
			SpliceRowsAtEditSite(item, buffer, firstLine);

			for (std::size_t line = firstLine; line <= lastLine && line < buffer.GetLineCount();
				 ++line)
				item.Rows.UpdateLine(buffer, line);
		}

		std::pair<std::size_t, std::size_t> BlockLines(const TextBuffer& buffer,
													   const Caret& caret)
		{
			const auto [from, to] = SelectionRange(caret);

			const std::size_t firstLine = buffer.ToPosition(from).Line;
			std::size_t lastLine = buffer.ToPosition(to).Line;

			if (lastLine > firstLine && to == buffer.LineStart(lastLine))
				--lastLine;

			return {firstLine, lastLine};
		}

		bool DeleteRange(OpenItem& item, Motion motion)
		{
			if (!Editable(item))
				return false;

			TextBuffer& buffer = item.Document->GetBuffer();

			const Caret target = HasSelection(item.Caret)
									 ? item.Caret
									 : Move(buffer, item.Caret, motion, true, item.TabWidth, 0);

			const auto [from, to] = SelectionRange(target);

			if (from == to)
				return false;

			const std::size_t firstLine = buffer.ToPosition(from).Line;

			buffer.BeginTransaction();
			buffer.Erase(from, to);
			buffer.EndTransaction();

			CollapseCaretTo(item.Caret, from);

			UpdateDerived(item, firstLine, firstLine);

			return true;
		}

		bool SettleAfterHistoryStep(OpenItem& item, std::size_t caretOffset)
		{
			TextBuffer& buffer = item.Document->GetBuffer();

			buffer.BreakUndoGroup();

			CollapseCaretTo(item.Caret, std::min(caretOffset, buffer.GetLength()));

			RefreshDerived(item);

			return true;
		}

		std::string LeadingWhitespace(std::string_view line)
		{
			const std::size_t end = line.find_first_not_of(" \t");

			return std::string{line.substr(0, end == std::string_view::npos ? line.size() : end)};
		}

		bool InsertAsOwnUndoGroup(OpenItem& item, TextBuffer& buffer, std::string_view text)
		{
			buffer.BreakUndoGroup();
			const bool changed = InsertText(item, text);
			buffer.BreakUndoGroup();

			return changed;
		}

		struct IndentRemoval
		{
			std::size_t Offset = 0;
			std::size_t Bytes = 0;
		};

		std::size_t OutdentBytesOf(std::string_view text, std::size_t spaceWidth)
		{
			if (!text.empty() && text[0] == '\t')
				return 1;

			std::size_t spaces = 0;

			while (spaces < spaceWidth && spaces < text.size() && text[spaces] == ' ')
				++spaces;

			return spaces;
		}

		std::vector<IndentRemoval> PlanOutdent(const TextBuffer& buffer, std::size_t firstLine,
											   std::size_t lastLine, std::size_t spaceWidth)
		{
			std::vector<IndentRemoval> removals;

			for (std::size_t line = firstLine; line <= lastLine; ++line)
			{
				const std::size_t bytes = OutdentBytesOf(buffer.LineText(line), spaceWidth);

				if (bytes > 0)
					removals.push_back(IndentRemoval{buffer.LineStart(line), bytes});
			}

			return removals;
		}

		void EraseBackToFront(TextBuffer& buffer, const std::vector<IndentRemoval>& removals)
		{
			for (auto entry = removals.rbegin(); entry != removals.rend(); ++entry)
				buffer.Erase(entry->Offset, entry->Offset + entry->Bytes);
		}

		// The run of leading whitespace every non-blank line in the block shares byte for byte.
		// Byte for byte rather than by width: a tab and four spaces are the same width and still
		// not the same text, and inserting the prefix "between" them would split one of them.
		std::size_t SharedIndentBytes(const TextBuffer& buffer, std::size_t firstLine,
									  std::size_t lastLine)
		{
			std::optional<std::string> shared;

			for (std::size_t line = firstLine; line <= lastLine; ++line)
			{
				if (IsBlankLine(buffer, line))
					continue;

				const std::string indent = LeadingWhitespace(buffer.LineText(line));

				if (!shared)
				{
					shared = indent;
					continue;
				}

				std::size_t common = 0;

				while (common < shared->size() && common < indent.size() &&
					   (*shared)[common] == indent[common])
					++common;

				shared->resize(common);
			}

			return shared ? shared->size() : 0;
		}

		bool EveryNonBlankLineIsCommented(const TextBuffer& buffer, std::size_t firstLine,
										  std::size_t lastLine, std::string_view prefix)
		{
			bool any = false;

			for (std::size_t line = firstLine; line <= lastLine; ++line)
			{
				if (IsBlankLine(buffer, line))
					continue;

				const std::string text = buffer.LineText(line);
				const std::size_t indent = LeadingWhitespace(text).size();

				if (std::string_view(text).substr(indent).rfind(prefix, 0) != 0)
					return false;

				any = true;
			}

			return any;
		}

		// Each commented line loses its prefix, and the one space after it when there is one, so
		// that commenting and uncommenting round-trip.
		std::vector<IndentRemoval> PlanUncomment(const TextBuffer& buffer, std::size_t firstLine,
												 std::size_t lastLine, std::string_view prefix)
		{
			std::vector<IndentRemoval> removals;

			for (std::size_t line = firstLine; line <= lastLine; ++line)
			{
				if (IsBlankLine(buffer, line))
					continue;

				const std::string text = buffer.LineText(line);
				const std::size_t indent = LeadingWhitespace(text).size();
				const std::size_t after = indent + prefix.size();
				const bool spaced = after < text.size() && text[after] == ' ';

				removals.push_back(IndentRemoval{buffer.LineStart(line) + indent,
												 prefix.size() + (spaced ? 1 : 0)});
			}

			return removals;
		}
	}

	void RefreshDerived(OpenItem& item)
	{
		const TextBuffer& buffer = item.Document->GetBuffer();

		item.IndentDepths = IndentDepths(buffer, item.TabWidth);
		item.WidestColumn = 0;
		item.WidestLine = 0;

		for (std::size_t line = 0; line < buffer.GetLineCount(); ++line)
		{
			const std::size_t width = ColumnCount(buffer.LineText(line), item.TabWidth);

			if (width > item.WidestColumn)
			{
				item.WidestColumn = width;
				item.WidestLine = line;
			}
		}

		// Rebuilt at the width already in force, not at kNoWrap, or a document on screen
		// would unwrap under the user until the window was next resized.
		item.Rows.Rebuild(buffer, item.Rows.GetColumnsPerRow(), item.TabWidth);
	}

	bool InsertText(OpenItem& item, std::string_view utf8)
	{
		if (!Editable(item) || utf8.empty())
			return false;

		const std::string text = NormaliseNewlines(utf8);
		TextBuffer& buffer = item.Document->GetBuffer();
		const auto [from, to] = SelectionRange(item.Caret);

		const std::size_t firstLine = buffer.ToPosition(from).Line;

		buffer.BeginTransaction();

		if (from != to)
			buffer.Erase(from, to);

		buffer.Insert(from, text);
		buffer.EndTransaction();

		CollapseCaretTo(item.Caret, from + text.size());

		UpdateDerived(item, firstLine, buffer.ToPosition(item.Caret.Offset).Line);

		return true;
	}

	bool DeleteBackward(OpenItem& item)
	{
		return DeleteRange(item, Motion::Left);
	}

	bool DeleteForward(OpenItem& item)
	{
		return DeleteRange(item, Motion::Right);
	}

	bool DeleteWordBackward(OpenItem& item)
	{
		return DeleteRange(item, Motion::WordLeft);
	}

	bool DeleteWordForward(OpenItem& item)
	{
		return DeleteRange(item, Motion::WordRight);
	}

	bool Undo(OpenItem& item)
	{
		// Not gated by Editable: every state in the history is one the buffer already held,
		// so a reload that turned the document read-only stays reversible.
		if (!item.Document)
			return false;

		const std::optional<std::size_t> caret = item.Document->GetBuffer().Undo();

		if (!caret)
			return false;

		return SettleAfterHistoryStep(item, *caret);
	}

	bool Redo(OpenItem& item)
	{
		// Ungated for the same reason as Undo.
		if (!item.Document)
			return false;

		const std::optional<std::size_t> caret = item.Document->GetBuffer().Redo();

		if (!caret)
			return false;

		return SettleAfterHistoryStep(item, *caret);
	}

	bool InsertNewline(OpenItem& item, IndentStyle /*style*/)
	{
		if (!Editable(item))
			return false;

		TextBuffer& buffer = item.Document->GetBuffer();
		const auto [from, to] = SelectionRange(item.Caret);
		const std::size_t line = buffer.ToPosition(from).Line;

		// Copied from the current line rather than synthesised from `style`, so a
		// tab-indented file stays tab-indented even under a spaces setting.
		const std::string insertion = "\n" + LeadingWhitespace(buffer.LineText(line));

		buffer.BeginTransaction();

		if (from != to)
			buffer.Erase(from, to);

		buffer.Insert(from, insertion);
		buffer.EndTransaction();

		CollapseCaretTo(item.Caret, from + insertion.size());

		UpdateDerived(item, line, buffer.ToPosition(item.Caret.Offset).Line);

		return true;
	}

	bool Indent(OpenItem& item, IndentStyle style)
	{
		if (!Editable(item))
			return false;

		const std::string indent =
			style.UseSpaces ? std::string(style.Width, ' ') : std::string{"\t"};

		TextBuffer& buffer = item.Document->GetBuffer();

		if (!HasSelection(item.Caret))
		{
			// Refused before the group is broken: InsertText would also report a width of
			// zero as "nothing changed", but only after an edit that never happened had
			// already split the surrounding typing in two.
			if (indent.empty())
				return false;

			return InsertAsOwnUndoGroup(item, buffer, indent);
		}

		const auto [firstLine, lastLine] = BlockLines(buffer, item.Caret);

		buffer.BreakUndoGroup();
		buffer.BeginTransaction();

		for (std::size_t line = lastLine + 1; line > firstLine; --line)
			buffer.Insert(buffer.LineStart(line - 1), indent);

		buffer.EndTransaction();
		buffer.BreakUndoGroup();

		SelectWholeLines(item.Caret, buffer, firstLine, lastLine);

		UpdateDerived(item, firstLine, lastLine);

		return true;
	}

	bool Outdent(OpenItem& item, IndentStyle style)
	{
		if (!Editable(item))
			return false;

		TextBuffer& buffer = item.Document->GetBuffer();
		const auto [firstLine, lastLine] = BlockLines(buffer, item.Caret);

		const std::vector<IndentRemoval> removals =
			PlanOutdent(buffer, firstLine, lastLine, style.Width);

		if (removals.empty())
			return false;

		buffer.BreakUndoGroup();
		buffer.BeginTransaction();

		EraseBackToFront(buffer, removals);

		buffer.EndTransaction();
		buffer.BreakUndoGroup();

		SelectWholeLines(item.Caret, buffer, firstLine, lastLine);

		UpdateDerived(item, firstLine, lastLine);

		return true;
	}

	bool ToggleLineComment(OpenItem& item, std::string_view prefix)
	{
		if (!Editable(item) || prefix.empty())
			return false;

		TextBuffer& buffer = item.Document->GetBuffer();
		const bool hadSelection = HasSelection(item.Caret);
		const auto [firstLine, lastLine] = BlockLines(buffer, item.Caret);

		const bool uncomment = EveryNonBlankLineIsCommented(buffer, firstLine, lastLine, prefix);

		// Measured before the edit: the caret's line is the only one whose offset the caret
		// needs to follow, and what happened to it is how far its start moved.
		const std::size_t caretLine = buffer.ToPosition(item.Caret.Offset).Line;
		const std::size_t caretLineLengthBefore = buffer.LineLength(caretLine);
		const std::size_t caretColumnBefore = item.Caret.Offset - buffer.LineStart(caretLine);

		buffer.BreakUndoGroup();
		buffer.BeginTransaction();

		bool changed = false;

		// Where the edit sits on the caret's line. With no selection that line is the only one
		// edited, so this is the column the caret is measured against afterwards.
		std::size_t editColumn = 0;

		if (uncomment)
		{
			const std::vector<IndentRemoval> removals =
				PlanUncomment(buffer, firstLine, lastLine, prefix);

			for (const IndentRemoval& removal : removals)
			{
				if (removal.Offset >= buffer.LineStart(caretLine) &&
					removal.Offset <= buffer.LineStart(caretLine) + caretLineLengthBefore)
				{
					editColumn = removal.Offset - buffer.LineStart(caretLine);
				}
			}

			EraseBackToFront(buffer, removals);
			changed = !removals.empty();
		}
		else
		{
			const std::size_t column = SharedIndentBytes(buffer, firstLine, lastLine);
			editColumn = column;
			const std::string inserted = std::string(prefix) + ' ';

			for (std::size_t line = lastLine + 1; line > firstLine; --line)
			{
				if (IsBlankLine(buffer, line - 1))
					continue;

				buffer.Insert(buffer.LineStart(line - 1) + column, inserted);
				changed = true;
			}
		}

		buffer.EndTransaction();
		buffer.BreakUndoGroup();

		if (!changed)
			return false;

		if (hadSelection)
		{
			SelectWholeLines(item.Caret, buffer, firstLine, lastLine);
		}
		else
		{
			// A caret before the edit, in the indentation ahead of it, has no text moving under
			// it and stays put. One at or after the edit follows its character by however much
			// the line grew or shrank; one inside a removed prefix lands where the prefix began.
			const std::size_t lengthAfter = buffer.LineLength(caretLine);
			std::size_t column = caretColumnBefore;

			if (caretColumnBefore >= editColumn)
			{
				const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(lengthAfter) -
											 static_cast<std::ptrdiff_t>(caretLineLengthBefore);
				const std::ptrdiff_t moved = static_cast<std::ptrdiff_t>(caretColumnBefore) + delta;

				column = static_cast<std::size_t>(
					std::max<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(editColumn), moved));
			}

			CollapseCaretTo(item.Caret,
							buffer.LineStart(caretLine) + std::min(column, lengthAfter));
		}

		UpdateDerived(item, firstLine, lastLine);

		return true;
	}
}
