#pragma once

#include "Core/Command/MenuBar.h"

#include <vector>

namespace Graphite
{
    /// The application's menu, as data. Every entry names a command registered by
    /// whoever owns the state it changes, so this file says what the menus contain and
    /// nothing about what the items do.
    [[nodiscard]] std::vector<Core::MenuItem> BuildMenuTree();
}
