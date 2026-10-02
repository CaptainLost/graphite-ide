#include "corepch.h"

#include "Core/Command/Chord.h"

#include "Core/Base/Ascii.h"
#include "Core/Command/KeyName.h"
#include "Core/Command/ModifierName.h"

namespace Core {

	namespace {
		constexpr const char* kWheelUpName = "WheelUp";
		constexpr const char* kWheelDownName = "WheelDown";

		bool NamesMatch(std::string_view left, std::string_view right)
		{
			return Lowered(left) == Lowered(right);
		}

		const ModifierName* ModifierNamed(std::string_view name)
		{
			for (const ModifierName& modifier : kModifiers)
			{
				if (NamesMatch(modifier.Name, name))
				{
					return &modifier;
				}
			}

			return nullptr;
		}

		// Consumes the leading "Ctrl+Shift+" run of `text`, leaving whatever the trigger is.
		// A trailing '+' therefore leaves an empty segment, which falls out as "no key named
		// that", and a '+' that is itself the key — "Ctrl+=", "Num+" — ends the run.
		ImGuiKeyChord TakeModifierPrefixes(std::string& text)
		{
			ImGuiKeyChord mods = 0;

			for (;;)
			{
				const std::size_t separator = text.find('+');

				if (separator == std::string::npos)
				{
					break;
				}

				const ModifierName* found =
					ModifierNamed(Trimmed(std::string_view(text).substr(0, separator)));

				if (found == nullptr)
				{
					break;
				}

				mods |= found->Bit;
				text = Trimmed(std::string_view(text).substr(separator + 1));
			}

			return mods;
		}

		std::string NameModifiersOf(ImGuiKeyChord keys)
		{
			std::string result;

			for (const ModifierName& modifier : kModifiers)
			{
				if ((keys & modifier.Bit) != 0)
				{
					result += modifier.Name;
					result += '+';
				}
			}

			return result;
		}
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

	Chord Chord::ModifierTap(ImGuiKeyChord mods)
	{
		Chord chord;
		chord.m_Keys = mods & ImGuiMod_Mask_;
		chord.m_Trigger = Trigger::ModifierTap;
		chord.m_Scope = Chord::Scope::Global;

		return chord;
	}

	bool Chord::IsEmpty() const
	{
		// A tap names its gesture with the modifiers alone, so it is the modifiers that have
		// to be there. Every other trigger says what it fires on by its own kind.
		if (m_Trigger == Trigger::ModifierTap)
		{
			return (m_Keys & ImGuiMod_Mask_) == 0;
		}

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

		std::string result = NameModifiersOf(m_Keys);

		// A tap has no trigger to append: the modifiers are the gesture, so the last one
		// stands where a key would, and the separator behind it goes.
		if (m_Trigger == Trigger::ModifierTap)
		{
			result.pop_back();
			return result;
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

		case Trigger::ModifierTap:
			// Returned above: a tap has no trigger to append.
			break;
		}

		return result;
	}

	std::optional<Chord> Chord::Parse(std::string_view text)
	{
		std::string remaining = Trimmed(text);

		if (remaining.empty())
		{
			return std::nullopt;
		}

		const ImGuiKeyChord mods = TakeModifierPrefixes(remaining);
		const std::string trigger = Trimmed(remaining);

		if (trigger.empty())
		{
			return std::nullopt;
		}

		if (NamesMatch(trigger, kWheelUpName))
		{
			return Chord::Wheel(mods, Trigger::WheelUp, Chord::Scope::HoveredEditor);
		}

		if (NamesMatch(trigger, kWheelDownName))
		{
			return Chord::Wheel(mods, Trigger::WheelDown, Chord::Scope::HoveredEditor);
		}

		// A trailing segment that is itself a modifier name means the gesture is the
		// modifiers: "Ctrl" is a tap, "Ctrl+Shift" is a tap of both. ImGui names the keyboard
		// keys LeftCtrl and RightCtrl, so the two forms cannot be mistaken for one another.
		if (const ModifierName* tapped = ModifierNamed(trigger))
		{
			return Chord::ModifierTap(mods | tapped->Bit);
		}

		const ImGuiKey key = KeyNamed(trigger);

		if (key == ImGuiKey_None)
		{
			return std::nullopt;
		}

		// ImGuiKey's named range also holds gamepad buttons, mouse aliases and ImGui's
		// internal mod slots, all of which KeyNamed answers about honestly. None of them can
		// be what a chord fires on. Refused here rather than in KeyNamed, so the printer and
		// the parser go on agreeing about one table.
		if (!IsKeyboardKey(key) || IsModifierKey(key))
		{
			return std::nullopt;
		}

		return Chord::Key(mods | key);
	}
}