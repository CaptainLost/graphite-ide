#include "corepch.h"

#include "Core/Command/Chord.h"

#include "Core/Base/Ascii.h"
#include "Core/Command/KeyName.h"
#include "Core/Command/ModifierName.h"

namespace Core {

	namespace {
		constexpr const char* kWheelUpName = "WheelUp";
		constexpr const char* kWheelDownName = "WheelDown";

		constexpr char kModifierSeparator = '+';

		constexpr ShortcutScope kWheelGestureScope = ShortcutScope::HoveredEditor;

		ImGuiKeyChord ModifiersOf(ImGuiKeyChord keys)
		{
			return keys & ImGuiMod_Mask_;
		}

		ImGuiKey KeyOf(ImGuiKeyChord keys)
		{
			return static_cast<ImGuiKey>(keys & ~ImGuiMod_Mask_);
		}

		const ModifierName* ModifierNamed(std::string_view name)
		{
			for (const ModifierName& modifier : kModifiers)
			{
				if (EqualsIgnoringCase(modifier.Name, name))
				{
					return &modifier;
				}
			}

			return nullptr;
		}

		// Not a split on every '+': a key name can contain one ("Num+"), so the run ends at the first segment that is not a modifier name.
		ImGuiKeyChord TakeLeadingModifiers(std::string& text)
		{
			ImGuiKeyChord modifiers = ImGuiMod_None;

			while (true)
			{
				const std::size_t separatorPosition = text.find(kModifierSeparator);

				if (separatorPosition == std::string::npos)
				{
					break;
				}

				const std::string_view segment = std::string_view(text).substr(0, separatorPosition);
				const ModifierName* modifier = ModifierNamed(Trimmed(segment));

				if (modifier == nullptr)
				{
					break;
				}

				modifiers |= modifier->Bit;
				text = Trimmed(std::string_view(text).substr(separatorPosition + 1));
			}

			return modifiers;
		}

		std::string ModifierPrefixOf(ImGuiKeyChord keys)
		{
			std::string modifierPrefix;

			for (const ModifierName& modifier : kModifiers)
			{
				const bool chordHasModifier = (keys & modifier.Bit) != 0;
				if (chordHasModifier)
				{
					modifierPrefix += modifier.Name;
					modifierPrefix += kModifierSeparator;
				}
			}

			return modifierPrefix;
		}

		void DropSeparatorAfterLastModifier(std::string& modifierPrefix)
		{
			modifierPrefix.pop_back();
		}
	}

	Chord Chord::Key(ImGuiKeyChord keys)
	{
		Chord chord;
		chord.m_Keys = keys;
		chord.m_Trigger = Trigger::Key;
		chord.m_Scope = ShortcutScope::Global;

		return chord;
	}

	Chord Chord::Wheel(ImGuiKeyChord modifiers, Trigger direction, ShortcutScope scope)
	{
		Chord chord;
		chord.m_Keys = ModifiersOf(modifiers);
		chord.m_Trigger = direction;
		chord.m_Scope = scope;

		return chord;
	}

	Chord Chord::ModifierTap(ImGuiKeyChord modifiers)
	{
		Chord chord;
		chord.m_Keys = ModifiersOf(modifiers);
		chord.m_Trigger = Trigger::ModifierTap;
		chord.m_Scope = ShortcutScope::Global;

		return chord;
	}

	Chord Chord::WithFlags(const ChordFlags& flags) const
	{
		Chord flagged = *this;
		flagged.m_Flags = flags;

		return flagged;
	}

	bool Chord::IsSameGesture(const Chord& other) const
	{
		return m_Keys == other.m_Keys && m_Trigger == other.m_Trigger && m_Scope == other.m_Scope;
	}

	bool Chord::IsEmpty() const
	{
		switch (m_Trigger)
		{
		case Trigger::Key:
			return KeyOf(m_Keys) == ImGuiKey_None;

		case Trigger::WheelUp:
		case Trigger::WheelDown:
			return false;

		case Trigger::ModifierTap:
			return ModifiersOf(m_Keys) == ImGuiMod_None;
		}

		return false;
	}

	std::string Chord::ToString() const
	{
		if (IsEmpty())
		{
			return {};
		}

		std::string text = ModifierPrefixOf(m_Keys);

		switch (m_Trigger)
		{
		case Trigger::WheelUp:
			text += kWheelUpName;
			break;

		case Trigger::WheelDown:
			text += kWheelDownName;
			break;

		case Trigger::Key:
			text += NameOfKey(KeyOf(m_Keys));
			break;

		case Trigger::ModifierTap:
			DropSeparatorAfterLastModifier(text);
			break;
		}

		return text;
	}

	std::optional<Chord> Chord::Parse(std::string_view text)
	{
		std::string remaining = Trimmed(text);

		if (remaining.empty())
		{
			return std::nullopt;
		}

		const ImGuiKeyChord modifiers = TakeLeadingModifiers(remaining);
		const std::string& triggerName = remaining;

		if (triggerName.empty())
		{
			return std::nullopt;
		}

		if (EqualsIgnoringCase(triggerName, kWheelUpName))
		{
			return Chord::Wheel(modifiers, Trigger::WheelUp, kWheelGestureScope);
		}

		if (EqualsIgnoringCase(triggerName, kWheelDownName))
		{
			return Chord::Wheel(modifiers, Trigger::WheelDown, kWheelGestureScope);
		}

		// ImGui names the keyboard keys LeftCtrl and RightCtrl, so a trigger that is a bare
		// modifier name can only mean a tap of the modifiers.
		const ModifierName* tappedModifier = ModifierNamed(triggerName);
		if (tappedModifier != nullptr)
		{
			return Chord::ModifierTap(modifiers | tappedModifier->Bit);
		}

		const ImGuiKey key = KeyNamed(triggerName);

		if (key == ImGuiKey_None)
		{
			return std::nullopt;
		}

		// Refused here rather than in KeyNamed, so that KeyNamed and NameOfKey keep agreeing
		// about one table of names.
		const bool keyCanFireChord = IsKeyboardKey(key) && !IsModifierKey(key);
		if (!keyCanFireChord)
		{
			return std::nullopt;
		}

		return Chord::Key(modifiers | key);
	}
}
