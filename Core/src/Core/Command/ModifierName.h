#pragma once

#include <imgui.h>

namespace Core
{
    struct ModifierName
    {
        ImGuiKeyChord bit;
        const char* name;
    };

    /// Order matters: it is the order they are printed in, so the same combination
    /// spells the same way however its bits arrived.
    constexpr ModifierName kModifiers[] = {
        {ImGuiMod_Ctrl, "Ctrl"},
        {ImGuiMod_Shift, "Shift"},
        {ImGuiMod_Alt, "Alt"},
        {ImGuiMod_Super, "Super"},
    };
}
