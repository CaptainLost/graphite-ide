#include "corepch.h"

#include "Core/Text/TextBuffer.h"

#include "Core/Base/Assert.h"

#include <algorithm>

namespace core::text
{
    TextBuffer::TextBuffer(std::string original) : m_original(std::move(original))
    {
        m_length = m_original.size();

        if (m_length > 0)
            m_pieces.push_back(Piece{Source::Original, 0, m_length, 0});

        m_lineStarts = BuildLineIndex();
    }

    const std::string& TextBuffer::SourceBuffer(Source source) const
    {
        return source == Source::Original ? m_original : m_append;
    }

    std::size_t TextBuffer::PieceAt(std::size_t offset) const
    {
        if (offset >= m_length)
            return m_pieces.size();

        // The first piece starting after `offset`, minus one, is the piece covering it.
        const auto after = std::upper_bound(
            m_pieces.begin(), m_pieces.end(), offset,
            [](std::size_t value, const Piece& piece) { return value < piece.start; });

        return static_cast<std::size_t>(after - m_pieces.begin()) - 1;
    }

    void TextBuffer::RefreshPieceStarts(std::size_t index)
    {
        std::size_t start = index == 0 ? 0 : m_pieces[index - 1].start + m_pieces[index - 1].length;

        for (std::size_t at = index; at < m_pieces.size(); ++at)
        {
            m_pieces[at].start = start;
            start += m_pieces[at].length;
        }

        m_length = start;
    }

    std::size_t TextBuffer::SplitAt(std::size_t offset)
    {
        if (offset >= m_length)
            return m_pieces.size();

        const std::size_t index = PieceAt(offset);
        const Piece& piece = m_pieces[index];

        if (piece.start == offset)
            return index;

        const std::size_t taken = offset - piece.start;

        Piece tail{piece.source, piece.offset + taken, piece.length - taken, offset};
        m_pieces[index].length = taken;
        m_pieces.insert(m_pieces.begin() + static_cast<std::ptrdiff_t>(index) + 1, tail);

        return index + 1;
    }

    std::size_t TextBuffer::InsertRaw(std::size_t offset, std::string_view text)
    {
        if (text.empty())
            return m_append.size();

        const std::size_t appended = m_append.size();
        m_append.append(text);

        InsertPieceRaw(offset, appended, text);

        return appended;
    }

    void TextBuffer::InsertPieceRaw(std::size_t offset, std::size_t appendOffset, std::string_view text)
    {
        const std::size_t at = std::min(offset, m_length);
        const std::size_t index = SplitAt(at);

        m_pieces.insert(m_pieces.begin() + static_cast<std::ptrdiff_t>(index),
                        Piece{Source::Append, appendOffset, text.size(), at});

        RefreshPieceStarts(index);

        PatchLineIndexForInsert(at, text);

        CORE_ASSERT(LineIndexMatchesRebuild(), "The patched line index no longer matches a rebuilt one");
    }

    std::size_t TextBuffer::PieceStartingAt(std::size_t offset) const
    {
        const auto found = std::lower_bound(
            m_pieces.begin(), m_pieces.end(), offset,
            [](const Piece& piece, std::size_t value) { return piece.start < value; });

        return static_cast<std::size_t>(found - m_pieces.begin());
    }

    void TextBuffer::EraseRaw(std::size_t from, std::size_t to)
    {
        const std::size_t end = std::min(to, m_length);

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

        m_pieces.erase(m_pieces.begin() + static_cast<std::ptrdiff_t>(first),
                       m_pieces.begin() + static_cast<std::ptrdiff_t>(last));

        RefreshPieceStarts(first);

        PatchLineIndexForErase(from, end);

        CORE_ASSERT(LineIndexMatchesRebuild(), "The patched line index no longer matches a rebuilt one");
    }

    std::string TextBuffer::Substring(std::size_t from, std::size_t to) const
    {
        const std::size_t end = std::min(to, m_length);

        if (from >= end)
            return {};

        std::string result;
        result.reserve(end - from);

        for (std::size_t index = PieceAt(from); index < m_pieces.size(); ++index)
        {
            const Piece& piece = m_pieces[index];

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
        if (offset >= m_length)
            return '\0';

        const Piece& piece = m_pieces[PieceAt(offset)];

        return SourceBuffer(piece.source)[piece.offset + (offset - piece.start)];
    }

    std::vector<std::size_t> TextBuffer::BuildLineIndex() const
    {
        std::vector<std::size_t> starts{0};
        const std::string content = Text();

        for (std::size_t offset = 0; offset < content.size(); ++offset)
        {
            if (content[offset] == '\n')
                starts.push_back(offset + 1);
        }

        return starts;
    }

    bool TextBuffer::LineIndexMatchesRebuild() const
    {
        return m_lineStarts == BuildLineIndex();
    }

    std::size_t TextBuffer::LineStart(std::size_t line) const
    {
        if (line >= m_lineStarts.size())
            return m_length;

        return m_lineStarts[line];
    }

    std::size_t TextBuffer::LineLength(std::size_t line) const
    {
        if (line >= m_lineStarts.size())
            return 0;

        // Every line but the last is followed by the '\n' that ended it, which is not
        // part of its text.
        const std::size_t end =
            line + 1 < m_lineStarts.size() ? m_lineStarts[line + 1] - 1 : m_length;

        return end - m_lineStarts[line];
    }

    std::string TextBuffer::LineText(std::size_t line) const
    {
        const std::size_t start = LineStart(line);

        return Substring(start, start + LineLength(line));
    }

    std::size_t TextBuffer::ToOffset(Position position) const
    {
        if (position.line >= m_lineStarts.size())
            return m_length;

        return m_lineStarts[position.line] + std::min(position.column, LineLength(position.line));
    }

    Position TextBuffer::ToPosition(std::size_t offset) const
    {
        const std::size_t at = std::min(offset, m_length);

        // The last line starting at or before `at` is the line `at` sits on.
        const auto after = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), at);
        const std::size_t line = static_cast<std::size_t>(after - m_lineStarts.begin()) - 1;

        return Position{line, at - m_lineStarts[line]};
    }

    void TextBuffer::PatchLineIndexForInsert(std::size_t offset, std::string_view text)
    {
        // The first line starting after the insertion point. Everything before it is
        // untouched, which is the whole reason for patching rather than rebuilding.
        const auto after = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), offset);
        const std::size_t index = static_cast<std::size_t>(after - m_lineStarts.begin());

        for (std::size_t at = index; at < m_lineStarts.size(); ++at)
            m_lineStarts[at] += text.size();

        std::vector<std::size_t> added;

        for (std::size_t at = 0; at < text.size(); ++at)
        {
            if (text[at] == '\n')
                added.push_back(offset + at + 1);
        }

        m_lineStarts.insert(m_lineStarts.begin() + static_cast<std::ptrdiff_t>(index),
                            added.begin(), added.end());
    }

    void TextBuffer::PatchLineIndexForErase(std::size_t from, std::size_t to)
    {
        const std::size_t removed = to - from;

        // A line start at s exists because of a '\n' at s - 1. That newline is inside
        // the erased range exactly when from < s <= to, so those starts go away and the
        // one at `from` itself, if any, stays.
        const auto first = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), from);
        const auto last = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), to);

        const std::size_t index = static_cast<std::size_t>(first - m_lineStarts.begin());
        m_lineStarts.erase(first, last);

        for (std::size_t at = index; at < m_lineStarts.size(); ++at)
            m_lineStarts[at] -= removed;
    }

    void TextBuffer::ApplyEdit(Edit& edit, bool reversed)
    {
        const bool inserting = reversed ? !edit.inserted : edit.inserted;

        if (inserting)
        {
            // The text may already live in m_append — from this edit's own original
            // insertion, or from an earlier undo of an erasure. Those bytes are never
            // removed or rewritten, so a range recorded once stays valid for every later
            // replay and nothing is appended twice.
            if (!edit.hasAppendOffset)
            {
                edit.appendOffset = m_append.size();
                m_append.append(edit.text);
                edit.hasAppendOffset = true;
            }

            InsertPieceRaw(edit.offset, edit.appendOffset, edit.text);
        }
        else
        {
            EraseRaw(edit.offset, edit.offset + edit.text.size());
        }
    }

    void TextBuffer::Insert(std::size_t offset, std::string_view text)
    {
        if (text.empty())
            return;

        const std::size_t at = std::min(offset, m_length);
        const std::size_t appendOffset = InsertRaw(at, text);

        m_redo.clear();
        m_undo.push_back(Edit{m_nextEditId++, at, std::string{text}, true, appendOffset, true});
    }

    void TextBuffer::Erase(std::size_t from, std::size_t to)
    {
        const std::size_t end = std::min(to, m_length);

        if (from >= end)
            return;

        std::string removed = Substring(from, end);

        EraseRaw(from, end);

        m_redo.clear();
        m_undo.push_back(Edit{m_nextEditId++, from, std::move(removed), false});
    }

    bool TextBuffer::Undo()
    {
        if (m_undo.empty())
            return false;

        Edit edit = std::move(m_undo.back());
        m_undo.pop_back();

        ApplyEdit(edit, true);
        m_redo.push_back(std::move(edit));

        return true;
    }

    bool TextBuffer::Redo()
    {
        if (m_redo.empty())
            return false;

        Edit edit = std::move(m_redo.back());
        m_redo.pop_back();

        ApplyEdit(edit, false);
        m_undo.push_back(std::move(edit));

        return true;
    }

    std::size_t TextBuffer::Revision() const noexcept
    {
        return m_undo.empty() ? 0 : m_undo.back().id;
    }
}
