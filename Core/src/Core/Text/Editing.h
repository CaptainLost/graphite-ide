#pragma once

#include <cstddef>
#include <string_view>

namespace core::text
{
    struct OpenItem;

    /// What Tab and automatic indentation insert.
    struct IndentStyle
    {
        bool useSpaces = true;
        std::size_t width = 4;
    };

    /// Every function here returns whether the document changed, leaves the caret,
    /// OpenItem::widestColumn, OpenItem::widestLine and OpenItem::indentDepths consistent
    /// with the new content, and does nothing at all to a read-only document.
    ///
    /// They take OpenItem rather than TextDocument because one edit touches four things
    /// at once — the content, the caret and the two caches — and the caches live here.
    /// Splitting that maintenance across Core and the ImGui layer would put half of it
    /// where nothing is tested.

    /// Inserts `utf8` at the caret, replacing the selection when there is one.
    ///
    /// This is the single entry point for text from outside the editor, so it is where
    /// line endings are normalised: the Windows clipboard is always CRLF, and
    /// TextBuffer::Insert requires LF. Both CRLF and a lone CR become LF.
    bool InsertText(OpenItem& item, std::string_view utf8);

    /// Removes the selection, or the character before the caret. Whole UTF-8 characters,
    /// never a byte of one.
    bool DeleteBackward(OpenItem& item);

    /// Removes the selection, or the character after the caret.
    bool DeleteForward(OpenItem& item);

    bool DeleteWordBackward(OpenItem& item);
    bool DeleteWordForward(OpenItem& item);

    /// Reverses or reapplies one gesture, then puts the caret where that edit happened
    /// and rebuilds the caches.
    ///
    /// Here rather than on TextBuffer because reversing an edit invalidates exactly what
    /// making one does, and two paths maintaining the same invariants is how they drift
    /// apart.
    bool Undo(OpenItem& item);
    bool Redo(OpenItem& item);

    /// Rebuilds widestColumn, widestLine and indentDepths by walking the document.
    ///
    /// Public because loading a document needs exactly the same three values, and the
    /// alternative is DocumentSet computing them its own way — two recipes for one set of
    /// invariants, which is how they end up disagreeing.
    void RefreshDerived(OpenItem& item);
}
