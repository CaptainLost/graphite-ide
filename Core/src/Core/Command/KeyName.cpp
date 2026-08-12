#include "corepch.h"

#include "Core/Command/KeyName.h"

#include "Core/Base/Ascii.h"

namespace core
{
    namespace
    {
        struct KeyName
        {
            ImGuiKey key;
            const char* name;
        };

        /// Only where ImGui's own name is not what a menu should show. GetKeyName calls
        /// ImGuiKey_Equal "Equal", and "Ctrl+Equal" beside a Zoom In item is worse than
        /// the "Ctrl+=" it replaces. Everything not listed here falls through to
        /// GetKeyName, which keeps this table from growing into a duplicate of ImGui's.
        constexpr KeyName kKeyNameOverrides[] = {
            {ImGuiKey_Equal, "="},         {ImGuiKey_Minus, "-"},
            {ImGuiKey_Comma, ","},         {ImGuiKey_Period, "."},
            {ImGuiKey_Slash, "/"},         {ImGuiKey_Backslash, "\\"},
            {ImGuiKey_Semicolon, ";"},     {ImGuiKey_Apostrophe, "'"},
            {ImGuiKey_LeftBracket, "["},   {ImGuiKey_RightBracket, "]"},
            {ImGuiKey_GraveAccent, "`"},   {ImGuiKey_KeypadAdd, "Num+"},
            {ImGuiKey_KeypadSubtract, "Num-"},
            {ImGuiKey_KeypadMultiply, "Num*"},
            {ImGuiKey_KeypadDivide, "Num/"},
            {ImGuiKey_KeypadDecimal, "Num."},
            {ImGuiKey_KeypadEnter, "NumEnter"},
        };
    }

    std::string NameOfKey(ImGuiKey key)
    {
        for (const KeyName& entry : kKeyNameOverrides)
        {
            if (entry.key == key)
            {
                return entry.name;
            }
        }

        // Named keypad digits are not in the override table — Keypad0..Keypad9 read
        // well enough as "Num0" once the prefix is swapped, and listing ten more
        // entries to say so would be noise.
        const std::string name = ImGui::GetKeyName(key);

        if (name.rfind("Keypad", 0) == 0)
        {
            return "Num" + name.substr(6);
        }

        return name;
    }

    bool IsModifierKey(ImGuiKey key) noexcept
    {
        if (key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper)
        {
            return true;
        }

        return key >= ImGuiKey_ReservedForModCtrl && key <= ImGuiKey_ReservedForModSuper;
    }

    ImGuiKey KeyNamed(std::string_view name)
    {
        const std::string wanted = ascii::Lowered(name);

        for (int index = ImGuiKey_NamedKey_BEGIN; index < ImGuiKey_NamedKey_END; ++index)
        {
            const auto key = static_cast<ImGuiKey>(index);

            if (ascii::Lowered(NameOfKey(key)) == wanted)
            {
                return key;
            }
        }

        return ImGuiKey_None;
    }
}
