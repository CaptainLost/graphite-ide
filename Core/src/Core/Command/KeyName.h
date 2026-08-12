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

    /// Whether `key` is a keyboard key rather than a gamepad button, a mouse alias or one
    /// of ImGui's internal mod slots.
    ///
    /// The named range is not the keyboard — that is the fact two separate bugs on this
    /// screen turned on. ImGuiKey_NamedKey_BEGIN..END covers four blocks in a row: the
    /// keyboard, ImGuiKey_Tab through ImGuiKey_Oem102 (imgui.h:1657-1712); the gamepad,
    /// ImGuiKey_GamepadStart through ImGuiKey_GamepadRStickDown (imgui.h:1717-1731); the
    /// mouse aliases, ImGuiKey_MouseLeft through ImGuiKey_MouseWheelY (imgui.h:1735); and
    /// the reserved mod slots (imgui.h:1738). Anything reading "a key the user pressed"
    /// out of that range gets mouse buttons too — and arming a capture is itself a left
    /// click, so the capture used to answer itself before the user could press anything.
    /// Stated as what a keyboard key is, rather than as a list of what to skip: the list
    /// is what went stale twice.
    [[nodiscard]] bool IsKeyboardKey(ImGuiKey key) noexcept;
}
