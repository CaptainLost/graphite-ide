#include "corepch.h"

#include "Core/Command/Chord.h"

#include "Core/Base/Ascii.h"
#include "Core/Command/KeyName.h"
#include "Core/Command/ModifierName.h"

namespace core
{
    namespace
    {
        constexpr const char* kWheelUpName = "WheelUp";
        constexpr const char* kWheelDownName = "WheelDown";
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
        std::string remaining = ascii::Trimmed(text);

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

            const std::string head = ascii::Lowered(ascii::Trimmed(std::string_view(remaining).substr(0, separator)));
            const ModifierName* found = nullptr;

            for (const ModifierName& modifier : kModifiers)
            {
                if (ascii::Lowered(modifier.name) == head)
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
            remaining = ascii::Trimmed(std::string_view(remaining).substr(separator + 1));
        }

        const std::string trigger = ascii::Trimmed(remaining);

        if (trigger.empty())
        {
            return std::nullopt;
        }

        if (ascii::Lowered(trigger) == ascii::Lowered(kWheelUpName))
        {
            return Chord::Wheel(mods, Trigger::WheelUp, Scope::HoveredEditor);
        }

        if (ascii::Lowered(trigger) == ascii::Lowered(kWheelDownName))
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
