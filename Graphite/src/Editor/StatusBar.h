#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Core
{
    class DocumentSet;
}

namespace Graphite
{
    /// The strip along the bottom of the window: the active document's path (with a
    /// dirty marker and, when it applies, the read-only reason) on the left, its caret
    /// position, encoding and line-ending style on the right, and - centred, while it is
    /// still fresh - a transient note such as "reloaded because it changed on disk".
    ///
    /// Must be called before DockSpaceHost::Begin. It uses BeginViewportSideBar, which
    /// shrinks the viewport work area the dockspace sizes itself from, so calling it after
    /// would leave the strip under the dockspace.
    ///
    /// With no active document the strip is empty: "Ln 1, Col 1" for something with no
    /// lines would be a lie. `transientMessage` is DocumentWindows::StatusMessage() -
    /// nullopt once it has expired - passed in rather than looked up here, since this
    /// function otherwise knows nothing about DocumentWindows.
    void DrawStatusBar(const Core::DocumentSet& documents,
                       const std::filesystem::path& root,
                       const std::optional<std::string>& transientMessage);
}
