#include "corepch.h"

#include "Core/Command/Chord.h"

#include "Core/Base/Ascii.h"
#include "Core/Command/KeyName.h"
#include "Core/Command/ModifierName.h"

namespace Core {

	namespace {
		constexpr const char* kWheelUpName = "WheelUp";
		constexpr const char* kWheelDownName = "WheelDown";
	}

	Chord Chord::Key(ImGuiKeyChord keys)
	{
		Chord chord;
		chord.m_Keys = keys;
		chord.m_Trigger = Trigger::Key;
		chord.m_Scope = Chord::Scope::Global;

		return chord;
	}

	Chord Chord::Wheel(ImGuiKeyChord mods, Trigger direction, Chord::Scope scope)
	{
		Chord chord;
		chord.m_Keys = mods & ImGuiMod_Mask_;
		chord.m_Trigger = direction;
		chord.m_Scope = scope;

		return chord;
	}

	bool Chord::IsEmpty() const
	{
		if (m_Trigger != Trigger::Key)
		{
			return false;
		}

		return (m_Keys & ~ImGuiMod_Mask_) == ImGuiKey_None;
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
			if ((m_Keys & modifier.bit) != 0)
			{
				result += modifier.name;
				result += '+';
			}
		}

		switch (m_Trigger)
		{
		case Trigger::WheelUp:
			result += kWheelUpName;
			break;

		case Trigger::WheelDown:
			result += kWheelDownName;
			break;

		case Trigger::Key:
			result += NameOfKey(static_cast<ImGuiKey>(m_Keys & ~ImGuiMod_Mask_));
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
			return Chord::Wheel(mods, Trigger::WheelUp, Chord::Scope::HoveredEditor);
		}

		if (Lowered(trigger) == Lowered(kWheelDownName))
		{
			return Chord::Wheel(mods, Trigger::WheelDown, Chord::Scope::HoveredEditor);
		}

		const ImGuiKey key = KeyNamed(trigger);

		if (key == ImGuiKey_None)
		{
			return std::nullopt;
		}

		// One rule: a chord's trigger is a keyboard key or a wheel direction, nothing else.
		//
		// ImGuiKey's named range also holds gamepad buttons, mouse aliases and ImGui's
		// internal mod slots, and KeyNamed answers about all of them honestly — "MouseLeft"
		// and "ModCtrl" really are names of keys. They just cannot be what a chord fires
		// on: a modifier is carried in the mods mask (which is why every modifier name was
		// stripped above), and a chord on MouseLeft would run its command at every click.
		// Refused here rather than in KeyNamed, so the printer and the parser go on
		// agreeing about one table.
		if (!IsKeyboardKey(key) || IsModifierKey(key))
		{
			return std::nullopt;
		}

		return Chord::Key(mods | key);
	}
}