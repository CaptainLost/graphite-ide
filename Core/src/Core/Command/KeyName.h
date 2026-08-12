#pragma once

#include <imgui.h>

#include <string>
#include <string_view>

namespace core
{
    [[nodiscard]] std::string NameOfKey(ImGuiKey key);

    /// ImGuiKey_None when no named key answers to `name`. Compared against the same
    /// NameOfKey the printer uses, so the two cannot drift.
    [[nodiscard]] ImGuiKey KeyNamed(std::string_view name);
}
