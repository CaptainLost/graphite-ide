#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace core::text
{
    /// A place in the document as the editor thinks of it. Both are zero-based, and the
    /// column counts bytes rather than characters — unambiguous, and no arithmetic
    /// between a position and the content it names. Moving by a character is
    /// NextCharacter/PreviousCharacter's job.
    struct Position
    {
        std::size_t line = 0;
        std::size_t column = 0;

        friend bool operator==(const Position&, const Position&) = default;
    };

    /// The content of an open document, as a piece table.
    ///
    /// The original content is read once and never written again; inserted text is
    /// appended to a second buffer that nothing is ever removed from; the document is a
    /// list of pieces pointing into one or the other. Neither inserting nor erasing
    /// copies content — they split, shorten and move entries in a vector.
    ///
    /// A vector rather than a linked list: pieces are few, and the entry covering an
    /// offset is found by binary search over the piece starts. A list would force a
    /// linear walk and scatter the entries across memory.
    ///
    /// Line endings are LF here. Normalising CRLF away is TextDocument's job and happens
    /// before the content reaches this class.
    class TextBuffer
    {
    public:
        TextBuffer() = default;
        explicit TextBuffer(std::string original);

        void Insert(std::size_t offset, std::string_view text);
        void Erase(std::size_t from, std::size_t to);

        [[nodiscard]] std::size_t Length() const noexcept { return m_length; }
        [[nodiscard]] bool Empty() const noexcept { return m_length == 0; }

        /// The content between two offsets. `to` is clamped to the end and a reversed
        /// range gives an empty string, so a caller cannot read out of bounds.
        [[nodiscard]] std::string Substring(std::size_t from, std::size_t to) const;
        [[nodiscard]] std::string Text() const { return Substring(0, m_length); }

        /// The byte at `offset`, or '\0' past the end.
        [[nodiscard]] char CharAt(std::size_t offset) const;

    private:
        enum class Source
        {
            Original,
            Append
        };

        struct Piece
        {
            Source source = Source::Original;
            /// Offset into the source buffer.
            std::size_t offset = 0;
            std::size_t length = 0;
            /// Offset into the document at which this piece begins. Redundant with the
            /// lengths before it, kept so the piece covering an offset is a binary
            /// search rather than a walk.
            std::size_t start = 0;
        };

        [[nodiscard]] const std::string& SourceBuffer(Source source) const;

        /// Index of the piece covering `offset`, or the piece count when `offset` is at
        /// or past the end.
        [[nodiscard]] std::size_t PieceAt(std::size_t offset) const;

        /// Splits the piece covering `offset` so that a piece boundary falls exactly
        /// there, and returns the index of the piece starting at `offset`.
        std::size_t SplitAt(std::size_t offset);

        /// Index of the first piece starting at or after `offset`, or the piece count
        /// when there is none. Only meaningful once a boundary has been split at
        /// `offset`.
        [[nodiscard]] std::size_t PieceStartingAt(std::size_t offset) const;

        /// Recomputes Piece::start from `index` onward. Every structural change ends
        /// here, which is what keeps the starts from drifting out of step with the
        /// lengths.
        void RefreshPieceStarts(std::size_t index);

        std::string m_original;
        std::string m_append;
        std::vector<Piece> m_pieces;
        std::size_t m_length = 0;
    };
}
