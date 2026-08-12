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
			return item.document && !item.document->IsReadOnly();
		}

		std::string NormaliseNewlines(std::string_view text)
		{
			std::string result;
			result.reserve(text.size());

			for (std::size_t index = 0; index < text.size(); ++index)
			{
				if (text[index] == '\r')
				{
					// Swallow the LF of a CRLF pair; a lone CR becomes LF on its own.
					if (index + 1 < text.size() && text[index + 1] == '\n')
						++index;

					result.push_back('\n');
					continue;
				}

				result.push_back(text[index]);
			}

			return result;
		}

		void UpdateIndentDepths(OpenItem& item, std::size_t firstLine, std::size_t lastLine)
		{
			const TextBuffer& buffer = item.document->GetBuffer();

			const std::size_t previousCount = item.indentDepths.size();
			const std::size_t currentCount = buffer.GetLineCount();

			// The vector is indexed by line, so lines added or removed at the edit site
			// shift every entry below them. resize() only pads or truncates the tail,
			// which pairs surviving depths with the wrong lines the moment the edit
			// isn't at the very end of the document; splicing at the edit site keeps
			// every depth below it lined up with the line it actually belongs to.
			//
			// `firstLine` is a valid index into the *old* line count in every caller —
			// each computes it from a position taken before the edit changed the line
			// count — but it is clamped anyway rather than trusting that indefinitely.
			const std::size_t at = std::min(firstLine, previousCount);

			if (currentCount > previousCount)
			{
				item.indentDepths.insert(
					item.indentDepths.begin() + static_cast<std::ptrdiff_t>(at),
					currentCount - previousCount, 0);
			}
			else if (currentCount < previousCount)
			{
				const std::size_t removed = previousCount - currentCount;

				// An edit only ever changes lines from `firstLine` onward, so the lines
				// that vanished lie within [at, previousCount) by construction — the end
				// is clamped too, so a future caller whose bookkeeping disagreed would
				// erase short of the vector's end rather than past it.
				const std::size_t end = std::min(at + removed, previousCount);

				item.indentDepths.erase(
					item.indentDepths.begin() + static_cast<std::ptrdiff_t>(at),
					item.indentDepths.begin() + static_cast<std::ptrdiff_t>(end));
			}

			const auto blank = [&buffer](std::size_t line) {
				const std::string text = buffer.LineText(line);
				return text.find_first_not_of(" \t") == std::string::npos;
			};

			std::size_t from = std::min(firstLine, buffer.GetLineCount() - 1);
			while (from > 0 && blank(from))
				--from;

			std::size_t to = std::min(lastLine, buffer.GetLineCount() - 1);
			while (to + 1 < buffer.GetLineCount() && blank(to))
				++to;

			// IndentDepths resolves blank lines against their neighbours, so it is given
			// the whole widened range at once rather than a line at a time. The range
			// overload returns entries indexed from `from`, not from line 0, so writing
			// them back means offsetting by `from` rather than indexing by line number
			// directly.
			const std::vector<std::size_t> depths = IndentDepths(buffer, DefaultTabWidth, from, to);

			for (std::size_t line = from; line <= to; ++line)
				item.indentDepths[line] = depths[line - from];
		}

		void UpdateDerived(OpenItem& item, std::size_t firstLine, std::size_t lastLine)
		{
			const TextBuffer& buffer = item.document->GetBuffer();

			if (item.widestLine >= buffer.GetLineCount() || item.widestLine >= firstLine)
			{
				RefreshDerived(item);
				return;
			}

			for (std::size_t line = firstLine; line <= lastLine && line < buffer.GetLineCount();
				 ++line)
			{
				const std::size_t width = ColumnCount(buffer.LineText(line), DefaultTabWidth);

				if (width > item.widestColumn)
				{
					item.widestColumn = width;
					item.widestLine = line;
				}
			}

			UpdateIndentDepths(item, firstLine, lastLine);

			// The row map is indexed by line, so lines added or removed at the edit site
			// shift every entry below them — the same problem UpdateIndentDepths solves
			// just above, spliced the same way and at the same point, so the two caches
			// cannot disagree about which line an entry belongs to. Nothing here walks the
			// whole document: the splice measures only the lines that are new, and the
			// loop after it re-measures only the lines the edit actually touched.
			//
			// The map still holds the pre-edit line count at this point — nothing has
			// touched it yet — so it is what says how many lines came or went.
			const std::size_t previousLines = item.rows.GetLineCount();
			const std::size_t currentLines = buffer.GetLineCount();
			const std::size_t at = std::min(firstLine, previousLines);

			if (currentLines > previousLines)
				item.rows.InsertLines(buffer, at, currentLines - previousLines);
			else if (currentLines < previousLines)
				item.rows.RemoveLines(at, previousLines - currentLines);

			for (std::size_t line = firstLine; line <= lastLine && line < currentLines; ++line)
				item.rows.UpdateLine(buffer, line);
		}

		std::pair<std::size_t, std::size_t> BlockLines(const TextBuffer& buffer,
													   const Caret& caret)
		{
			const auto [from, to] = SelectionRange(caret);

			const std::size_t firstLine = buffer.ToPosition(from).line;
			std::size_t lastLine = buffer.ToPosition(to).line;

			if (lastLine > firstLine && to == buffer.LineStart(lastLine))
				--lastLine;

			return {firstLine, lastLine};
		}

		bool DeleteRange(OpenItem& item, Motion motion)
		{
			if (!Editable(item))
				return false;

			TextBuffer& buffer = item.document->GetBuffer();

			// Move rather than NextCharacter/PreviousCharacter: those take a
			// string_view and would need the surrounding bytes cut out and the offsets
			// translated back, while Move already does exactly this over a buffer and
			// its rules — whole UTF-8 characters, whole words — are the ones CaretTests
			// already covers.
			Caret target = HasSelection(item.caret)
							   ? item.caret
							   : Move(buffer, item.caret, motion, true, DefaultTabWidth, 0);

			const auto [from, to] = SelectionRange(target);

			if (from == to)
				return false;

			const std::size_t firstLine = buffer.ToPosition(from).line;

			buffer.BeginTransaction();
			buffer.Erase(from, to);
			buffer.EndTransaction();

			item.caret.offset = from;
			item.caret.anchor = from;
			item.caret.desiredColumn = NoDesiredColumn;

			UpdateDerived(item, firstLine, firstLine);

			return true;
		}
	}

	void RefreshDerived(OpenItem& item)
	{
		const TextBuffer& buffer = item.document->GetBuffer();

		item.indentDepths = IndentDepths(buffer, DefaultTabWidth);
		item.widestColumn = 0;
		item.widestLine = 0;

		for (std::size_t line = 0; line < buffer.GetLineCount(); ++line)
		{
			const std::size_t width = ColumnCount(buffer.LineText(line), DefaultTabWidth);

			if (width > item.widestColumn)
			{
				item.widestColumn = width;
				item.widestLine = line;
			}
		}

		// At the width already in force, not at kNoWrap: this runs after an undo and after
		// any edit that changed the line count, both of which happen while the document is
		// on screen and wrapping to a width only the widget knows. Rebuilding unwrapped
		// here would unwrap the document under the user until the next time the window was
		// resized. A map that has never been built reports kNoWrap, which is exactly the
		// unwrapped editor a freshly loaded document should start as.
		item.rows.Rebuild(buffer, item.rows.GetColumnsPerRow(), DefaultTabWidth);
	}

	bool InsertText(OpenItem& item, std::string_view utf8)
	{
		if (!Editable(item) || utf8.empty())
			return false;

		const std::string text = NormaliseNewlines(utf8);
		TextBuffer& buffer = item.document->GetBuffer();
		const auto [from, to] = SelectionRange(item.caret);

		const std::size_t firstLine = buffer.ToPosition(from).line;

		buffer.BeginTransaction();

		if (from != to)
			buffer.Erase(from, to);

		buffer.Insert(from, text);
		buffer.EndTransaction();

		item.caret.offset = from + text.size();
		item.caret.anchor = item.caret.offset;
		item.caret.desiredColumn = NoDesiredColumn;

		UpdateDerived(item, firstLine, buffer.ToPosition(item.caret.offset).line);

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
		// Deliberately not guarded by Editable. The read-only flag exists to stop UTF-8
		// being inserted among bytes in some other encoding, and reversing an edit the
		// buffer already recorded introduces no new bytes: every state in the history is
		// one the buffer legitimately held - a whole content from a load or a reload, or
		// the result of edits made while the document was editable - so GetBytes() round
		// trips from any of them.
		//
		// Without this, a reload that turns the document read-only cannot be undone. A
		// clean UTF-8 file rewritten by another tool as Latin-1 reloads silently, which
		// the design justifies precisely by the reload being undoable; the previous text
		// would otherwise sit on the undo stack unreachable.
		if (!item.document || !item.document->GetBuffer().Undo())
			return false;

		TextBuffer& buffer = item.document->GetBuffer();

		buffer.BreakUndoGroup();
		item.caret.offset = std::min(item.caret.offset, buffer.GetLength());
		item.caret.anchor = item.caret.offset;
		item.caret.desiredColumn = NoDesiredColumn;

		RefreshDerived(item);

		return true;
	}

	bool Redo(OpenItem& item)
	{
		// Ungated for the same reason as Undo above: it only ever restores a state the
		// buffer already held.
		if (!item.document || !item.document->GetBuffer().Redo())
			return false;

		TextBuffer& buffer = item.document->GetBuffer();

		buffer.BreakUndoGroup();
		item.caret.offset = std::min(item.caret.offset, buffer.GetLength());
		item.caret.anchor = item.caret.offset;
		item.caret.desiredColumn = NoDesiredColumn;

		RefreshDerived(item);

		return true;
	}

	bool InsertNewline(OpenItem& item, IndentStyle /*style*/)
	{
		if (!Editable(item))
			return false;

		TextBuffer& buffer = item.document->GetBuffer();
		const auto [from, to] = SelectionRange(item.caret);
		const std::size_t line = buffer.ToPosition(from).line;

		// Copied verbatim from the current line rather than synthesised from `style`: a
		// tab-indented file should stay tab-indented for the line below it even under a
		// spaces setting. The file's own lines are the better authority.
		const std::string text = buffer.LineText(line);
		const std::size_t indentEnd = text.find_first_not_of(" \t");
		const std::string indent =
			text.substr(0, indentEnd == std::string::npos ? text.size() : indentEnd);

		const std::string insertion = "\n" + indent;

		buffer.BeginTransaction();

		if (from != to)
			buffer.Erase(from, to);

		buffer.Insert(from, insertion);
		buffer.EndTransaction();

		item.caret.offset = from + insertion.size();
		item.caret.anchor = item.caret.offset;
		item.caret.desiredColumn = NoDesiredColumn;

		UpdateDerived(item, line, buffer.ToPosition(item.caret.offset).line);

		return true;
	}

	bool Indent(OpenItem& item, IndentStyle style)
	{
		if (!Editable(item))
			return false;

		const std::string indent =
			style.useSpaces ? std::string(style.width, ' ') : std::string{"\t"};

		TextBuffer& buffer = item.document->GetBuffer();

		if (!HasSelection(item.caret))
		{
			// A width of zero would make this insert nothing; InsertText would say so by
			// returning false, but only after the group had already been broken for an
			// edit that never happened.
			if (indent.empty())
				return false;

			// Bracketed exactly as the block form below is, and as Outdent is either way:
			// one press of Tab is one press of Ctrl+Z, whether or not anything was
			// selected and whichever direction it went. Left to coalesce with the typing
			// around it, Tab and Shift+Tab would answer differently to the same gesture.
			buffer.BreakUndoGroup();
			const bool changed = InsertText(item, indent);
			buffer.BreakUndoGroup();

			return changed;
		}

		const auto [firstLine, lastLine] = BlockLines(buffer, item.caret);

		buffer.BreakUndoGroup();
		buffer.BeginTransaction();

		for (std::size_t line = lastLine + 1; line > firstLine; --line)
			buffer.Insert(buffer.LineStart(line - 1), indent);

		buffer.EndTransaction();
		buffer.BreakUndoGroup();

		// The block stays selected, so Tab pressed again indents the same lines rather
		// than replacing them with a tab character.
		item.caret.anchor = buffer.LineStart(firstLine);
		item.caret.offset = buffer.LineStart(lastLine) + buffer.LineLength(lastLine);
		item.caret.desiredColumn = NoDesiredColumn;

		UpdateDerived(item, firstLine, lastLine);

		return true;
	}

	bool Outdent(OpenItem& item, IndentStyle style)
	{
		if (!Editable(item))
			return false;

		TextBuffer& buffer = item.document->GetBuffer();
		const auto [firstLine, lastLine] = BlockLines(buffer, item.caret);

		// Measured before the buffer is touched at all, so that having nothing to remove
		// costs no transaction and — the part that was actually wrong — no
		// BreakUndoGroup. Shift+Tab against a line with no indentation used to break the
		// group the typing around it belonged to, splitting one gesture's worth of undo
		// in two over an edit that never happened.
		//
		// Each entry is a line start and how many bytes to drop from it.
		std::vector<std::pair<std::size_t, std::size_t>> removals;

		for (std::size_t line = firstLine; line <= lastLine; ++line)
		{
			const std::string text = buffer.LineText(line);

			std::size_t remove = 0;

			if (!text.empty() && text[0] == '\t')
			{
				remove = 1;
			}
			else
			{
				while (remove < style.width && remove < text.size() && text[remove] == ' ')
					++remove;
			}

			if (remove > 0)
				removals.emplace_back(buffer.LineStart(line), remove);
		}

		if (removals.empty())
			return false;

		buffer.BreakUndoGroup();
		buffer.BeginTransaction();

		// Back to front, as before: erasing a line's indent moves every offset after it,
		// and the starts still to be used are the ones above.
		for (auto entry = removals.rbegin(); entry != removals.rend(); ++entry)
			buffer.Erase(entry->first, entry->first + entry->second);

		buffer.EndTransaction();
		buffer.BreakUndoGroup();

		item.caret.anchor = buffer.LineStart(firstLine);
		item.caret.offset = buffer.LineStart(lastLine) + buffer.LineLength(lastLine);
		item.caret.desiredColumn = NoDesiredColumn;

		UpdateDerived(item, firstLine, lastLine);

		return true;
	}
}