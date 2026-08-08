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

    void TextBuffer::Insert(std::size_t offset, std::string_view text)
    {
        if (text.empty())
            return;

        const std::size_t at = std::min(offset, m_length);
        const std::size_t index = SplitAt(at);

        const std::size_t appended = m_append.size();
        m_append.append(text);

        m_pieces.insert(m_pieces.begin() + static_cast<std::ptrdiff_t>(index),
                        Piece{Source::Append, appended, text.size(), at});

        RefreshPieceStarts(index);
    }

    std::size_t TextBuffer::PieceStartingAt(std::size_t offset) const
    {
        const auto found = std::lower_bound(
            m_pieces.begin(), m_pieces.end(), offset,
            [](const Piece& piece, std::size_t value) { return piece.start < value; });

        return static_cast<std::size_t>(found - m_pieces.begin());
    }

    void TextBuffer::Erase(std::size_t from, std::size_t to)
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
}
