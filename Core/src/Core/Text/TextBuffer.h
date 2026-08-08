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

        /// Reverses the last edit. False when there is nothing left to reverse.
        bool Undo();

        /// Reapplies the last undone edit. False when there is nothing to reapply.
        bool Redo();

        [[nodiscard]] bool CanUndo() const noexcept { return !m_undo.empty(); }
        [[nodiscard]] bool CanRedo() const noexcept { return !m_redo.empty(); }

        /// Identifies the edit currently on top of the undo stack, or 0 for none.
        ///
        /// TextDocument remembers this at each save and compares to decide whether the
        /// document is modified. An identity rather than a depth: undoing one edit and
        /// making a different one returns the stack to the same depth while the content
        /// differs, and a depth would call that saved.
        [[nodiscard]] std::size_t Revision() const noexcept;

        [[nodiscard]] std::size_t Length() const noexcept { return m_length; }
        [[nodiscard]] bool Empty() const noexcept { return m_length == 0; }

        /// The content between two offsets. `to` is clamped to the end and a reversed
        /// range gives an empty string, so a caller cannot read out of bounds.
        [[nodiscard]] std::string Substring(std::size_t from, std::size_t to) const;
        [[nodiscard]] std::string Text() const { return Substring(0, m_length); }

        /// The byte at `offset`, or '\0' past the end.
        [[nodiscard]] char CharAt(std::size_t offset) const;

        /// Never zero: an empty document is one empty line.
        [[nodiscard]] std::size_t LineCount() const noexcept { return m_lineStarts.size(); }

        /// Offset the line begins at. Past the last line, the end of the document.
        [[nodiscard]] std::size_t LineStart(std::size_t line) const;

        /// Length of the line's content, excluding the '\n' that ends it.
        [[nodiscard]] std::size_t LineLength(std::size_t line) const;

        /// The line's content, without its line ending.
        [[nodiscard]] std::string LineText(std::size_t line) const;

        [[nodiscard]] std::size_t ToOffset(Position position) const;
        [[nodiscard]] Position ToPosition(std::size_t offset) const;

        /// Whether the incrementally patched line index still equals one built from
        /// scratch. Asserted after every edit in Debug, and checked directly by the
        /// tests — this is the subtlest arithmetic in the editor and the one place a bug
        /// would stay hidden longest.
        [[nodiscard]] bool LineIndexMatchesRebuild() const;

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

        /// Builds the index by walking the content. Deliberately the obvious algorithm:
        /// it is the yardstick the patched index is measured against.
        [[nodiscard]] std::vector<std::size_t> BuildLineIndex() const;

        void PatchLineIndexForInsert(std::size_t offset, std::string_view text);
        void PatchLineIndexForErase(std::size_t from, std::size_t to);

        /// The edit itself, with no undo record. Insert/Erase wrap these; undo and redo
        /// go straight to them so replaying an edit does not record a new one.
        void InsertRaw(std::size_t offset, std::string_view text);
        void EraseRaw(std::size_t from, std::size_t to);

        /// One reversible edit. An insertion of `text` at `offset` inverts into an
        /// erasure of the same range, and the other way round.
        struct Edit
        {
            std::size_t id = 0;
            std::size_t offset = 0;
            std::string text;
            bool inserted = false;
        };

        /// Applies an edit without recording it, which is what undo and redo do.
        void ApplyEdit(const Edit& edit, bool reversed);

        std::string m_original;
        std::string m_append;
        std::vector<Piece> m_pieces;
        std::size_t m_length = 0;
        std::vector<std::size_t> m_lineStarts{0};
        std::vector<Edit> m_undo;
        std::vector<Edit> m_redo;
        std::size_t m_nextEditId = 1;
    };
}
