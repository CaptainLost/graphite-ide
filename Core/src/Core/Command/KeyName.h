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

    /// Whether `key` is a modifier rather than something a chord can be triggered by.
    ///
    /// There are two spans of them, which is the whole reason this is a function and not
    /// a comparison written where it is needed. The eight keys a keyboard has, at
    /// imgui.h:1662-1663, and four aliases ImGui reserves for the modifier bits it stores
    /// in a key chord — ImGuiKey_ReservedForModCtrl..ReservedForModSuper, at imgui.h:1736,
    /// far from the first span and squarely inside ImGuiKey_NamedKey_BEGIN..END. Anything
    /// walking the named-key range sees them, ImGui::GetKeyName calls them "ModCtrl" and
    /// friends, and holding Ctrl raises them: a rebinding screen that missed them recorded
    /// "Ctrl+ModCtrl" the moment a user reached for Ctrl.
    [[nodiscard]] bool IsModifierKey(ImGuiKey key) noexcept;
}
