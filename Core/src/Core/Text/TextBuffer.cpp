#include "corepch.h"

#include "Core/Text/TextBuffer.h"

#include "Core/Base/Assert.h"

#include <algorithm>

namespace Core {

	TextBuffer::TextBuffer(std::string original) : m_Original(std::move(original))
	{
		m_Length = m_Original.size();

		if (m_Length > 0)
			m_Pieces.push_back(Piece{Source::Original, 0, m_Length, 0});

		m_LineStarts = BuildLineIndex();
	}

	const std::string& TextBuffer::SourceBuffer(Source source) const
	{
		return source == Source::Original ? m_Original : m_Append;
	}

	std::size_t TextBuffer::PieceAt(std::size_t offset) const
	{
		if (offset >= m_Length)
			return m_Pieces.size();

		// The first piece starting after `offset`, minus one, is the piece covering it.
		const auto after = std::upper_bound(
			m_Pieces.begin(), m_Pieces.end(), offset,
			[](std::size_t value, const Piece& piece) { return value < piece.start; });

		return static_cast<std::size_t>(after - m_Pieces.begin()) - 1;
	}

	void TextBuffer::RefreshPieceStarts(std::size_t index)
	{
		std::size_t start = index == 0 ? 0 : m_Pieces[index - 1].start + m_Pieces[index - 1].length;

		for (std::size_t at = index; at < m_Pieces.size(); ++at)
		{
			m_Pieces[at].start = start;
			start += m_Pieces[at].length;
		}

		m_Length = start;
	}

	std::size_t TextBuffer::SplitAt(std::size_t offset)
	{
		if (offset >= m_Length)
			return m_Pieces.size();

		const std::size_t index = PieceAt(offset);
		const Piece& piece = m_Pieces[index];

		if (piece.start == offset)
			return index;

		const std::size_t taken = offset - piece.start;

		Piece tail{piece.source, piece.offset + taken, piece.length - taken, offset};
		m_Pieces[index].length = taken;
		m_Pieces.insert(m_Pieces.begin() + static_cast<std::ptrdiff_t>(index) + 1, tail);

		return index + 1;
	}

	std::size_t TextBuffer::InsertRaw(std::size_t offset, std::string_view text)
	{
		if (text.empty())
			return m_Append.size();

		const std::size_t appended = m_Append.size();
		m_Append.append(text);

		InsertPieceRaw(offset, appended, text);

		return appended;
	}

	void TextBuffer::InsertPieceRaw(std::size_t offset, std::size_t appendOffset, std::string_view text)
	{
		const std::size_t at = std::min(offset, m_Length);
		const std::size_t index = SplitAt(at);

		m_Pieces.insert(m_Pieces.begin() + static_cast<std::ptrdiff_t>(index),
						Piece{Source::Append, appendOffset, text.size(), at});

		RefreshPieceStarts(index);

		PatchLineIndexForInsert(at, text);

		CORE_ASSERT(LineIndexMatchesRebuild(), "The patched line index no longer matches a rebuilt one");
	}

	std::size_t TextBuffer::PieceStartingAt(std::size_t offset) const
	{
		const auto found = std::lower_bound(
			m_Pieces.begin(), m_Pieces.end(), offset,
			[](const Piece& piece, std::size_t value) { return piece.start < value; });

		return static_cast<std::size_t>(found - m_Pieces.begin());
	}

	void TextBuffer::EraseRaw(std::size_t from, std::size_t to)
	{
		const std::size_t end = std::min(to, m_Length);

		if (from >= end)
			return;

		// Split at both ends first, so the range to remove is a whole run of pieces and
		// there is no partial piece left to trim afterwards.
		//
		// The indices are looked up *after* both splits rather than taken from their
		// return values: splitting at `from` inserts a piece ahead of the boundary at
		// `end`, which would push the index the earlier split reported one place out of
		// date. Two lookups are cheaper than that bug.
		SplitAt(end);
		SplitAt(from);

		const std::size_t first = PieceStartingAt(from);
		const std::size_t last = PieceStartingAt(end);

		CORE_ASSERT(first <= last, "Erase found its bounds out of order");

		m_Pieces.erase(m_Pieces.begin() + static_cast<std::ptrdiff_t>(first),
					   m_Pieces.begin() + static_cast<std::ptrdiff_t>(last));

		RefreshPieceStarts(first);

		PatchLineIndexForErase(from, end);

		CORE_ASSERT(LineIndexMatchesRebuild(), "The patched line index no longer matches a rebuilt one");
	}

	std::string TextBuffer::Substring(std::size_t from, std::size_t to) const
	{
		const std::size_t end = std::min(to, m_Length);

		if (from >= end)
			return {};

		std::string result;
		result.reserve(end - from);

		for (std::size_t index = PieceAt(from); index < m_Pieces.size(); ++index)
		{
			const Piece& piece = m_Pieces[index];

			if (piece.start >= end)
				break;

			const std::size_t sliceFrom = from > piece.start ? from - piece.start : 0;
			const std::size_t sliceTo = std::min(piece.length, end - piece.start);

			result.append(SourceBuffer(piece.source), piece.offset + sliceFrom,
						  sliceTo - sliceFrom);
		}

		return result;
	}

	char TextBuffer::CharAt(std::size_t offset) const
	{
		if (offset >= m_Length)
			return '\0';

		const Piece& piece = m_Pieces[PieceAt(offset)];

		return SourceBuffer(piece.source)[piece.offset + (offset - piece.start)];
	}

	std::vector<std::size_t> TextBuffer::BuildLineIndex() const
	{
		std::vector<std::size_t> starts{0};
		const std::string content = Materialize();

		for (std::size_t offset = 0; offset < content.size(); ++offset)
		{
			if (content[offset] == '\n')
				starts.push_back(offset + 1);
		}

		return starts;
	}

	bool TextBuffer::LineIndexMatchesRebuild() const
	{
		return m_LineStarts == BuildLineIndex();
	}

	std::size_t TextBuffer::LineStart(std::size_t line) const
	{
		if (line >= m_LineStarts.size())
			return m_Length;

		return m_LineStarts[line];
	}

	std::size_t TextBuffer::LineLength(std::size_t line) const
	{
		if (line >= m_LineStarts.size())
			return 0;

		// Every line but the last is followed by the '\n' that ended it, which is not
		// part of its text.
		const std::size_t end =
			line + 1 < m_LineStarts.size() ? m_LineStarts[line + 1] - 1 : m_Length;

		return end - m_LineStarts[line];
	}

	std::string TextBuffer::LineText(std::size_t line) const
	{
		const std::size_t start = LineStart(line);

		return Substring(start, start + LineLength(line));
	}

	std::size_t TextBuffer::ToOffset(Position position) const
	{
		if (position.Line >= m_LineStarts.size())
			return m_Length;

		return m_LineStarts[position.Line] + std::min(position.Column, LineLength(position.Line));
	}

	Position TextBuffer::ToPosition(std::size_t offset) const
	{
		const std::size_t at = std::min(offset, m_Length);

		// The last line starting at or before `at` is the line `at` sits on.
		const auto after = std::upper_bound(m_LineStarts.begin(), m_LineStarts.end(), at);
		const std::size_t line = static_cast<std::size_t>(after - m_LineStarts.begin()) - 1;

		return Position{line, at - m_LineStarts[line]};
	}

	void TextBuffer::PatchLineIndexForInsert(std::size_t offset, std::string_view text)
	{
		// The first line starting after the insertion point. Everything before it is
		// untouched, which is the whole reason for patching rather than rebuilding.
		const auto after = std::upper_bound(m_LineStarts.begin(), m_LineStarts.end(), offset);
		const std::size_t index = static_cast<std::size_t>(after - m_LineStarts.begin());

		for (std::size_t at = index; at < m_LineStarts.size(); ++at)
			m_LineStarts[at] += text.size();

		std::vector<std::size_t> added;

		for (std::size_t at = 0; at < text.size(); ++at)
		{
			if (text[at] == '\n')
				added.push_back(offset + at + 1);
		}

		m_LineStarts.insert(m_LineStarts.begin() + static_cast<std::ptrdiff_t>(index),
							added.begin(), added.end());
	}

	void TextBuffer::PatchLineIndexForErase(std::size_t from, std::size_t to)
	{
		const std::size_t removed = to - from;

		// A line start at s exists because of a '\n' at s - 1. That newline is inside
		// the erased range exactly when from < s <= to, so those starts go away and the
		// one at `from` itself, if any, stays.
		const auto first = std::upper_bound(m_LineStarts.begin(), m_LineStarts.end(), from);
		const auto last = std::upper_bound(m_LineStarts.begin(), m_LineStarts.end(), to);

		const std::size_t index = static_cast<std::size_t>(first - m_LineStarts.begin());
		m_LineStarts.erase(first, last);

		for (std::size_t at = index; at < m_LineStarts.size(); ++at)
			m_LineStarts[at] -= removed;
	}

	void TextBuffer::ApplyEdit(Edit& edit, bool reversed)
	{
		const bool inserting = reversed ? !edit.inserted : edit.inserted;

		if (inserting)
		{
			// The text may already live in m_Append — from this edit's own original
			// insertion, or from an earlier undo of an erasure. Those bytes are never
			// removed or rewritten, so a range recorded once stays valid for every later
			// replay and nothing is appended twice.
			if (!edit.hasAppendOffset)
			{
				edit.appendOffset = m_Append.size();
				m_Append.append(edit.text);
				edit.hasAppendOffset = true;
			}

			InsertPieceRaw(edit.offset, edit.appendOffset, edit.text);
		}
		else
		{
			EraseRaw(edit.offset, edit.offset + edit.text.size());
		}
	}

	void TextBuffer::BeginTransaction()
	{
		if (m_TransactionDepth++ == 0)
			m_TransactionGroup = 0;
	}

	void TextBuffer::EndTransaction()
	{
		if (m_TransactionDepth > 0)
			--m_TransactionDepth;
	}

	std::size_t TextBuffer::GroupForNewEdit(std::size_t from, std::size_t to,
											std::string_view text, bool inserted)
	{
		const bool hasNewline = text.find('\n') != std::string_view::npos;

		std::size_t group;

		// Inside a transaction every edit joins the one this transaction opened, so the
		// whole gesture reverses together.
		if (m_TransactionDepth > 0 && m_TransactionGroup != 0)
		{
			group = m_TransactionGroup;
		}
		else
		{
			const bool continues =
				!m_BreakGroup && !hasNewline && !m_Undo.empty() && (inserted ? m_LastGroupKind == GroupKind::Insertion && from == m_InsertEnd : m_LastGroupKind == GroupKind::Deletion && (to == m_DeleteAt || from == m_DeleteAt));

			group = continues ? m_Undo.back().group : m_NextGroupId++;

			if (m_TransactionDepth > 0)
				m_TransactionGroup = group;
		}

		// Runs for every edit, including one that took the transaction shortcut above, so
		// these describe the group's *last* edit rather than its first — a transaction's
		// second edit can be the one that lands a newline or turns an erase-then-insert
		// into something a following keystroke should still coalesce with.
		m_LastGroupKind = inserted ? GroupKind::Insertion : GroupKind::Deletion;
		m_InsertEnd = inserted ? from + text.size() : from;
		m_DeleteAt = from;

		// A group holding a newline is closed behind itself: Enter is a boundary a user
		// expects undo to stop at, and it is where an accidental merge would be most
		// confusing.
		m_BreakGroup = hasNewline;

		return group;
	}

	void TextBuffer::Insert(std::size_t offset, std::string_view text)
	{
		if (text.empty())
			return;

		const std::size_t at = std::min(offset, m_Length);
		const std::size_t appendOffset = InsertRaw(at, text);

		m_Redo.clear();
		m_Undo.push_back(Edit{m_NextEditId++, at, std::string{text}, true, appendOffset, true,
							  GroupForNewEdit(at, at, text, true)});
	}

	void TextBuffer::Erase(std::size_t from, std::size_t to)
	{
		const std::size_t end = std::min(to, m_Length);

		if (from >= end)
			return;

		std::string removed = Substring(from, end);

		EraseRaw(from, end);

		const std::size_t group = GroupForNewEdit(from, end, removed, false);

		m_Redo.clear();
		m_Undo.push_back(Edit{m_NextEditId++, from, std::move(removed), false, 0, false, group});
	}

	bool TextBuffer::Undo()
	{
		if (m_Undo.empty())
			return false;

		const std::size_t group = m_Undo.back().group;

		while (!m_Undo.empty() && m_Undo.back().group == group)
		{
			Edit edit = std::move(m_Undo.back());
			m_Undo.pop_back();

			ApplyEdit(edit, true);
			m_Redo.push_back(std::move(edit));
		}

		m_BreakGroup = true;
		m_LastGroupKind = GroupKind::None;

		return true;
	}

	bool TextBuffer::Redo()
	{
		if (m_Redo.empty())
			return false;

		const std::size_t group = m_Redo.back().group;

		while (!m_Redo.empty() && m_Redo.back().group == group)
		{
			Edit edit = std::move(m_Redo.back());
			m_Redo.pop_back();

			ApplyEdit(edit, false);
			m_Undo.push_back(std::move(edit));
		}

		m_BreakGroup = true;
		m_LastGroupKind = GroupKind::None;

		return true;
	}

	std::size_t TextBuffer::GetRevision() const
	{
		return m_Undo.empty() ? 0 : m_Undo.back().id;
	}
}