#pragma once

#include <cstddef>
#include <limits>
#include <utility>

namespace Core
{
    class TextBuffer;

    /// `Caret::desiredColumn` when the caret has not aimed at a column yet. A fresh caret
    /// starts here, as does one a caller built directly rather than through `Move` or
    /// `PlaceAt`; `SelectWordAt`, `SelectLineAt` and `SelectAll` also leave their result at
    /// this sentinel, since a selection has no column to resume aiming at. Vertical motion
    /// resolves it from the caret's actual current column on first use; every other
    /// motion — `Move`'s non-vertical branches and `PlaceAt` — always writes a real column
    /// instead, so the field means exactly one of two things: "aiming here" or "not aiming
    /// anywhere yet."
    inline constexpr std::size_t NoDesiredColumn = std::numeric_limits<std::size_t>::max();

    /// Where the user is in a document, and what is selected.
    struct Caret
    {
        /// Canonical position, in bytes. Always on a UTF-8 character boundary.
        std::size_t offset = 0;

        /// Where the selection started. Equal to `offset` means nothing is selected.
        std::size_t anchor = 0;

        /// The column vertical movement aims for, or `NoDesiredColumn` when nothing has
        /// set one yet.
        ///
        /// Without it, moving down through a line shorter than the current column sticks
        /// the caret to that short line's end and loses the column for every line after —
        /// which is noticed immediately. Every motion but vertical movement resolves this
        /// to the caret's real column (`PlaceAt` is the supported way to reposition a
        /// caret and get a correct one); vertical movement carries the previous value
        /// forward untouched, or resolves `NoDesiredColumn` from the caret's actual column
        /// if it is the first vertical move in a chain.
        std::size_t desiredColumn = NoDesiredColumn;

        friend bool operator==(const Caret&, const Caret&) = default;
    };

    enum class Motion
    {
        Left,
        Right,
        Up,
        Down,
        LineStart,
        LineEnd,
        DocumentStart,
        DocumentEnd,
        WordLeft,
        WordRight,
        PageUp,
        PageDown
    };

    [[nodiscard]] bool HasSelection(const Caret& caret);

    /// The selection as an ordered half-open range, so dragging left reads the same as
    /// dragging right.
    [[nodiscard]] std::pair<std::size_t, std::size_t> SelectionRange(const Caret& caret);

    /// Applies a motion. `extend` keeps the anchor, which is what Shift does; without it
    /// the anchor collapses onto the new position. `pageLines` is how many lines fit on
    /// screen — Core does not know how tall the window is, so the caller passes it.
    [[nodiscard]] Caret Move(const TextBuffer& buffer, Caret caret, Motion motion, bool extend,
                             std::size_t tabWidth, std::size_t pageLines);

    /// Puts the caret at a byte offset, as a mouse click does. The offset is clamped to
    /// the document and pulled back to a character boundary.
    [[nodiscard]] Caret PlaceAt(const TextBuffer& buffer, Caret caret, std::size_t offset,
                                bool extend, std::size_t tabWidth);

    /// Selects the run of like characters around `offset`, as a double click does.
    ///
    /// A run is letters, whitespace, or punctuation — whichever class the character at
    /// the offset belongs to. Bytes outside ASCII count as letters, which makes
    /// `variableName_ąę` one word. That is a deliberate simplification, not an
    /// implementation of the Unicode word rules.
    [[nodiscard]] Caret SelectWordAt(const TextBuffer& buffer, std::size_t offset);

    /// Selects a whole line including its ending, as a triple click does.
    [[nodiscard]] Caret SelectLineAt(const TextBuffer& buffer, std::size_t line);

    [[nodiscard]] Caret SelectAll(const TextBuffer& buffer);
}
