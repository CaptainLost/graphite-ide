#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Command/MenuItem.h"

#include <functional>
#include <vector>

namespace core
{
    class CommandRegistry;
    class Keymap;

    /// Draws the host window's menu bar from a tree of command ids.
    class MenuBar final : NonCopyable
    {
    public:
        /// `rowHeight` is the bar's height in pixels, or zero to leave ImGui's own
        /// FramePadding-derived height alone — the same value DockLayout::menuBarHeight
        /// carries, and it has to match, or the items sit at the wrong height for the row.
        ///
        /// `trailing` is submitted inside the bar after the last menu, on the same row.
        /// It is the one escape hatch and it is narrow on purpose: an application's own
        /// caption — a version label, window buttons — has to be drawn between
        /// BeginMenuBar and EndMenuBar, and is the one thing Core cannot know about.
        void Render(CommandRegistry& commands, const Keymap& keys,
                    const std::vector<MenuItem>& tree, float rowHeight,
                    const std::function<void()>& trailing);
    };
}
