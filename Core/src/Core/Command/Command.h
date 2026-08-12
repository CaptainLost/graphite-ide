#pragma once

#include <functional>
#include <string>

namespace Core
{
    /// Something the application can be asked to do, named once and reachable from
    /// everywhere that can ask: a menu item, a key binding, a button. The id is the name
    /// — bindings are stored under it, so it outlives labels and menu positions and must
    /// not be renamed lightly.
    struct Command
    {
        std::string id;
        std::string label;

        /// A menu group this command joins, or empty. Groups exist for menus whose
        /// contents are not known when the menu tree is built — the panel toggles, which
        /// arrive as panels are added.
        std::string group;

        std::function<void()> run;

        /// Empty means always enabled. Asked every time the command is drawn or run, not
        /// cached: the answer changes with the workspace, the selection and the document.
        std::function<bool()> enabled;

        /// Empty means this is not a toggle. Drives the tick beside a menu item.
        std::function<bool()> checked;
    };
}
