#pragma once

#include <filesystem>

namespace core::text
{
    class DocumentSet;
}

namespace graphite
{
    /// The strip along the bottom of the window: the active document's path on the left,
    /// its caret position, encoding and line-ending style on the right.
    ///
    /// Must be called before DockSpaceHost::Begin. It uses BeginViewportSideBar, which
    /// shrinks the viewport work area the dockspace sizes itself from, so calling it after
    /// would leave the strip under the dockspace.
    ///
    /// With no active document the strip is empty: "Ln 1, Col 1" for something with no
    /// lines would be a lie.
    void DrawStatusBar(const core::text::DocumentSet& documents,
                       const std::filesystem::path& root);
}
