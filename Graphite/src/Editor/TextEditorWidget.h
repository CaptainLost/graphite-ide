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
    /// Input handling arrives in a later task; this draws.
    void DrawEditor(core::text::OpenItem& item, float fontSize);
}
