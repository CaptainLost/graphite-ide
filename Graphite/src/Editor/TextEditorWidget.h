#pragma once

namespace core::text
{
    struct OpenItem;
}

namespace graphite
{
    /// Draws one open item into the current ImGui window: a line-number gutter, the
    /// visible lines, the current-line highlight, the selection and the caret.
    ///
    /// Only the visible lines are drawn, through ImGuiListClipper, so a million-line file
    /// costs the same as a hundred-line one. An item with no document draws a centred,
    /// dimmed explanation instead.
    ///
    /// Also reads mouse and keyboard input for the surface it draws: clicking and
    /// dragging place the caret and select text, and the arrow keys, Home/End,
    /// Page Up/Down, Ctrl+A and Ctrl+C work while this document's own child window has
    /// keyboard focus. The caret and selection live on `item.caret`, so they survive a
    /// tab switch.
    void DrawEditor(core::text::OpenItem& item, float fontSize);
}
