#include "corepch.h"

#include "Core/Ui/Chord.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace core
{
    namespace
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

        constexpr const char* kWheelUpName = "WheelUp";
        constexpr const char* kWheelDownName = "WheelDown";

        [[nodiscard]] std::string NameOfKey(ImGuiKey key)
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

        [[nodiscard]] std::string Lowered(std::string_view text)
        {
            std::string result;
            result.reserve(text.size());

            for (const char character : text)
            {
                // Cast through unsigned char: tolower on a negative char is undefined,
                // and a settings file can hold any byte.
                result.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character))));
            }

            return result;
        }

        [[nodiscard]] std::string Trimmed(std::string_view text)
        {
            const auto notSpace = [](unsigned char character) { return std::isspace(character) == 0; };

            std::size_t begin = 0;
            while (begin < text.size() && !notSpace(static_cast<unsigned char>(text[begin])))
            {
                ++begin;
            }

            std::size_t end = text.size();
            while (end > begin && !notSpace(static_cast<unsigned char>(text[end - 1])))
            {
                --end;
            }

            return std::string(text.substr(begin, end - begin));
        }

        /// ImGuiKey_None when no named key answers to `name`. Compared against the same
        /// NameOfKey the printer uses, so the two cannot drift.
        [[nodiscard]] ImGuiKey KeyNamed(std::string_view name)
        {
            const std::string wanted = Lowered(name);

            for (int index = ImGuiKey_NamedKey_BEGIN; index < ImGuiKey_NamedKey_END; ++index)
            {
                const auto key = static_cast<ImGuiKey>(index);

                if (Lowered(NameOfKey(key)) == wanted)
                {
                    return key;
                }
            }

            return ImGuiKey_None;
        }
    }

    Chord Chord::Key(ImGuiKeyChord keys)
    {
        Chord chord;
        chord.m_keys = keys;
        chord.m_trigger = Trigger::Key;
        chord.m_scope = Scope::Global;

        return chord;
    }

    Chord Chord::Wheel(ImGuiKeyChord mods, Trigger direction, Scope scope)
    {
        Chord chord;
        chord.m_keys = mods & ImGuiMod_Mask_;
        chord.m_trigger = direction;
        chord.m_scope = scope;

        return chord;
    }

    bool Chord::IsEmpty() const noexcept
    {
        if (m_trigger != Trigger::Key)
        {
            return false;
        }

        return (m_keys & ~ImGuiMod_Mask_) == ImGuiKey_None;
    }

    std::string Chord::ToString() const
    {
        if (IsEmpty())
        {
            return {};
        }

        std::string result;

        for (const ModifierName& modifier : kModifiers)
        {
            if ((m_keys & modifier.bit) != 0)
            {
                result += modifier.name;
                result += '+';
            }
        }

        switch (m_trigger)
        {
            case Trigger::WheelUp:
                result += kWheelUpName;
                break;

            case Trigger::WheelDown:
                result += kWheelDownName;
                break;

            case Trigger::Key:
                result += NameOfKey(static_cast<ImGuiKey>(m_keys & ~ImGuiMod_Mask_));
                break;
        }

        return result;
    }

    std::optional<Chord> Chord::Parse(std::string_view text)
    {
        ImGuiKeyChord mods = 0;
        std::string remaining = Trimmed(text);

        if (remaining.empty())
        {
            return std::nullopt;
        }

        // Modifiers are stripped from the front one at a time, so the last segment is
        // whatever the trigger is. A trailing '+' therefore leaves an empty segment,
        // which falls out below as "no key named that".
        for (;;)
        {
            const std::size_t separator = remaining.find('+');

            if (separator == std::string::npos)
            {
                break;
            }

            const std::string head = Lowered(Trimmed(std::string_view(remaining).substr(0, separator)));
            const ModifierName* found = nullptr;

            for (const ModifierName& modifier : kModifiers)
            {
                if (Lowered(modifier.name) == head)
                {
                    found = &modifier;
                }
            }

            if (found == nullptr)
            {
                // Not a modifier, so this '+' is the key itself — "Ctrl+=" and "Num+".
                break;
            }

            mods |= found->bit;
            remaining = Trimmed(std::string_view(remaining).substr(separator + 1));
        }

        const std::string trigger = Trimmed(remaining);

        if (trigger.empty())
        {
            return std::nullopt;
        }

        if (Lowered(trigger) == Lowered(kWheelUpName))
        {
            return Chord::Wheel(mods, Trigger::WheelUp, Scope::HoveredEditor);
        }

        if (Lowered(trigger) == Lowered(kWheelDownName))
        {
            return Chord::Wheel(mods, Trigger::WheelDown, Scope::HoveredEditor);
        }

        const ImGuiKey key = KeyNamed(trigger);

        if (key == ImGuiKey_None)
        {
            return std::nullopt;
        }

        return Chord::Key(mods | key);
    }
}
