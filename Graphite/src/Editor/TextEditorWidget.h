#pragma once

#include "Core/Text/Editing.h"

namespace core::text
{
    struct OpenItem;
}

namespace graphite
{
    /// What one call to DrawEditor produced, for the caller to act on.
    struct EditorFrameResult
    {
        /// The wheel delta the user applied with Ctrl held, or 0.
        float zoomDelta = 0.0f;

        /// True when an edit this frame actually changed the document. The caller
        /// promotes a preview tab on this, the same as a double click on its tab does.
        bool edited = false;

        /// True when Ctrl+S was pressed this frame. The widget never saves on its own:
        /// whether a stale document needs confirming before it can be written is the
        /// caller's question, so this only reports the request.
        bool saveRequested = false;
    };

    /// Draws one open item into the current ImGui window: a line-number gutter, the
    /// visible lines, the current-line highlight, the selection and the caret.
    ///
    /// Only the visible lines are drawn, through ImGuiListClipper, so a million-line file
    /// costs the same as a hundred-line one. An item with no document draws a centred,
    /// dimmed explanation instead.
    ///
    /// Also reads mouse and keyboard input for the surface it draws: clicking and
    /// dragging place the caret and select text; the arrow keys, Home/End, Page Up/Down,
    /// Ctrl+A and Ctrl+C work; and typing, Enter, Backspace, Delete, Tab/Shift+Tab,
    /// Ctrl+V, Ctrl+X, Ctrl+Z, Ctrl+Y/Ctrl+Shift+Z edit the document, all while this
    /// document's own child window has keyboard focus. The caret and selection live on
    /// `item.caret`, so they survive a tab switch.
    ///
    /// `indentStyle` governs what Tab, Enter's auto-indent and the newline splitter
    /// insert. `wordWrap` makes a line too wide for the window occupy several screen rows
    /// instead of running off to the right. The caller owns the font size, the indent
    /// style and the wrap setting, since all three are shared by every document and
    /// persisted.
    ///
    /// Drawing goes through `item.rows` either way: with wrapping off the map is built
    /// with RowMap::kNoWrap, which gives one row per line — the same picture as before,
    /// down the same code path, rather than a second renderer to keep in step.
    [[nodiscard]] EditorFrameResult DrawEditor(core::text::OpenItem& item, float fontSize,
                                               core::text::IndentStyle indentStyle,
                                               bool wordWrap);
}
