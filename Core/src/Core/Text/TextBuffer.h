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

        /// Inserts `text` at `offset`, clamped to the end.
        ///
        /// Precondition: `text` already uses LF line endings. Nothing here normalises
        /// CRLF — TextDocument::FromContent does that once, on load, before content ever
        /// reaches this class. A caller inserting text from an external source (stage 3's
        /// paste from the Windows clipboard, which is always CRLF) must normalise it
        /// first, or a stray '\r' ends up sitting in the buffer as an empty glyph at the
        /// end of a line and Bytes() emits "\r\r\n" for it.
        void Insert(std::size_t offset, std::string_view text);
        void Erase(std::size_t from, std::size_t to);

        /// Reverses the last edit. False when there is nothing left to reverse.
        bool Undo();

        /// Reapplies the last undone edit. False when there is nothing to reapply.
        bool Redo();

        /// Groups every edit made until the matching EndTransaction into one undo step.
        /// Nests: only the outermost EndTransaction closes the group.
        void BeginTransaction();
        void EndTransaction();

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

        /// Bytes held in the append-only buffer that inserted text lands in. Exists for
        /// diagnostics and so tests can confirm that toggling undo/redo does not grow it
        /// — an edit's text is appended once and every replay reuses that range.
        [[nodiscard]] std::size_t AppendBufferSize() const noexcept { return m_append.size(); }

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
        ///
        /// InsertRaw returns the offset the text actually landed at in m_append, so
        /// Insert can record it rather than recomputing m_append.size() itself and
        /// trusting the two calls to agree.
        std::size_t InsertRaw(std::size_t offset, std::string_view text);
        void EraseRaw(std::size_t from, std::size_t to);

        /// Inserts a piece that reuses `text.size()` bytes already sitting in m_append at
        /// `appendOffset`, instead of appending a fresh copy. The half of InsertRaw that
        /// is not "put bytes in m_append" — shared by a fresh insertion and by replaying
        /// one whose text is already there.
        void InsertPieceRaw(std::size_t offset, std::size_t appendOffset, std::string_view text);

        /// One reversible edit. An insertion of `text` at `offset` inverts into an
        /// erasure of the same range, and the other way round.
        struct Edit
        {
            std::size_t id = 0;
            std::size_t offset = 0;
            std::string text;
            bool inserted = false;

            /// Where `text` lives in m_append, once it has been placed there. Set
            /// immediately for an insertion, whose text is appended as soon as the edit
            /// is made; left unset for an erasure until its first undo re-inserts the
            /// removed text, which until then may live in the original buffer, in
            /// m_append from an earlier edit, or split across both. Every later replay
            /// of the same edit reuses this range rather than appending another copy —
            /// the bytes are never rewritten or removed once appended, so a recorded
            /// range stays valid for the buffer's lifetime.
            std::size_t appendOffset = 0;
            bool hasAppendOffset = false;

            /// Which gesture this edit belongs to. Undo and redo process every edit
            /// sharing the group on top of the stack, because one gesture can be several
            /// edits — typing over a selection is an erase and an insert, and undoing
            /// only half of that would leave the typed character sitting inside the text
            /// it replaced.
            std::size_t group = 0;
        };

        /// Applies an edit without recording it, which is what undo and redo do. Takes
        /// the edit by mutable reference to fill in appendOffset the first time its text
        /// is materialised into m_append.
        void ApplyEdit(Edit& edit, bool reversed);

        /// The group id a newly recorded edit belongs to.
        std::size_t GroupForNewEdit();

        std::string m_original;
        std::string m_append;
        std::vector<Piece> m_pieces;
        std::size_t m_length = 0;
        std::vector<std::size_t> m_lineStarts{0};
        std::vector<Edit> m_undo;
        std::vector<Edit> m_redo;
        std::size_t m_nextEditId = 1;
        std::size_t m_nextGroupId = 1;
        std::size_t m_transactionDepth = 0;
        /// The group this transaction has already opened, or 0 before its first edit.
        std::size_t m_transactionGroup = 0;
    };
}
