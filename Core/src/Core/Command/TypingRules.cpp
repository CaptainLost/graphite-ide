#include "corepch.h"

#include "Core/Command/TypingRules.h"

#include "Core/Command/Trigger.h"

#include <algorithm>
#include <array>

namespace Core {

	namespace {
		constexpr ImGuiKeyChord kCaretModifiers = ImGuiMod_Ctrl | ImGuiMod_Shift;

		constexpr std::array kCharacterKeys{
			ImGuiKey_Space,	   ImGuiKey_Apostrophe,	 ImGuiKey_Comma,		 ImGuiKey_Minus,		  ImGuiKey_Period,
			ImGuiKey_Slash,	   ImGuiKey_Semicolon,	 ImGuiKey_Equal,		 ImGuiKey_LeftBracket,	  ImGuiKey_Backslash,
			ImGuiKey_RightBracket, ImGuiKey_GraveAccent, ImGuiKey_Oem102,	 ImGuiKey_KeypadDecimal,  ImGuiKey_KeypadDivide,
			ImGuiKey_KeypadMultiply, ImGuiKey_KeypadSubtract, ImGuiKey_KeypadAdd, ImGuiKey_KeypadEqual,
		};

		constexpr std::array kCaretKeys{
			ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home,
			ImGuiKey_End,		ImGuiKey_PageUp,	 ImGuiKey_PageDown, ImGuiKey_Backspace, ImGuiKey_Delete,
			ImGuiKey_Insert,	ImGuiKey_Enter,		 ImGuiKey_KeypadEnter, ImGuiKey_Escape,	 ImGuiKey_Tab,
		};

		constexpr std::array kClipboardAndUndoKeys{ImGuiKey_A, ImGuiKey_C, ImGuiKey_V, ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};

		ImGuiKeyChord ModifiersOf(const Chord& chord)
		{
			return chord.GetKeys() & ImGuiMod_Mask_;
		}

		ImGuiKey KeyOf(const Chord& chord)
		{
			return static_cast<ImGuiKey>(chord.GetKeys() & ~ImGuiMod_Mask_);
		}

		bool IsBetween(ImGuiKey key, ImGuiKey first, ImGuiKey last)
		{
			return key >= first && key <= last;
		}

		template <std::size_t Count>
		bool IsOneOf(ImGuiKey key, const std::array<ImGuiKey, Count>& keys)
		{
			return std::ranges::find(keys, key) != keys.end();
		}

		bool HoldsOnly(ImGuiKeyChord modifiers, ImGuiKeyChord allowed)
		{
			return (modifiers & ~allowed) == ImGuiMod_None;
		}

		bool IsCharacterKey(ImGuiKey key)
		{
			const bool digit = IsBetween(key, ImGuiKey_0, ImGuiKey_9);
			const bool letter = IsBetween(key, ImGuiKey_A, ImGuiKey_Z);
			const bool keypadDigit = IsBetween(key, ImGuiKey_Keypad0, ImGuiKey_Keypad9);

			return digit || letter || keypadDigit || IsOneOf(key, kCharacterKeys);
		}

		std::optional<bool>* FlagFor(ChordFlags& flags, TypingTarget target)
		{
			switch (target)
			{
			case TypingTarget::Editor:
				return &flags.FiresInEditor;
			case TypingTarget::Terminal:
				return &flags.FiresInTerminal;
			case TypingTarget::TextField:
				return &flags.FiresInTextFields;
			case TypingTarget::Nothing:
				break;
			}

			return nullptr;
		}
	}

	bool TypesCharacter(const Chord& chord)
	{
		if (chord.GetTrigger() != Trigger::Key)
		{
			return false;
		}

		return HoldsOnly(ModifiersOf(chord), ImGuiMod_Shift) && IsCharacterKey(KeyOf(chord));
	}

	bool EditsText(const Chord& chord)
	{
		if (chord.GetTrigger() != Trigger::Key)
		{
			return false;
		}

		const ImGuiKeyChord modifiers = ModifiersOf(chord);
		const ImGuiKey key = KeyOf(chord);

		const bool movesOrEdits = IsOneOf(key, kCaretKeys) && HoldsOnly(modifiers, kCaretModifiers);
		const bool controlHeld = (modifiers & ImGuiMod_Ctrl) != ImGuiMod_None;
		const bool clipboardOrUndo = IsOneOf(key, kClipboardAndUndoKeys) && controlHeld && HoldsOnly(modifiers, kCaretModifiers);

		return movesOrEdits || clipboardOrUndo;
	}

	bool HasTypingFlags(const Chord& chord)
	{
		const Trigger trigger = chord.GetTrigger();
		const bool keyOrTap = trigger == Trigger::Key || trigger == Trigger::ModifierTap;

		return keyOrTap && !chord.IsEmpty();
	}

	bool CanRepeat(const Chord& chord)
	{
		return chord.GetTrigger() == Trigger::Key && !chord.IsEmpty();
	}

	bool FiresInByDefault(const Chord& chord, TypingTarget target)
	{
		switch (target)
		{
		case TypingTarget::Nothing:
			return true;
		case TypingTarget::Editor:
		case TypingTarget::Terminal:
			return !TypesCharacter(chord);
		case TypingTarget::TextField:
			return !TypesCharacter(chord) && !EditsText(chord);
		}

		return true;
	}

	std::optional<bool> FiresInFlag(const ChordFlags& flags, TypingTarget target)
	{
		switch (target)
		{
		case TypingTarget::Editor:
			return flags.FiresInEditor;
		case TypingTarget::Terminal:
			return flags.FiresInTerminal;
		case TypingTarget::TextField:
			return flags.FiresInTextFields;
		case TypingTarget::Nothing:
			break;
		}

		return std::nullopt;
	}

	bool FiresIn(const Chord& chord, TypingTarget target)
	{
		return FiresInFlag(chord.GetFlags(), target).value_or(FiresInByDefault(chord, target));
	}

	void SetFiresIn(ChordFlags& flags, const Chord& chord, TypingTarget target, bool fires)
	{
		std::optional<bool>* flag = FlagFor(flags, target);

		if (flag == nullptr)
		{
			return;
		}

		const bool matchesDefault = fires == FiresInByDefault(chord, target);
		*flag = matchesDefault ? std::nullopt : std::optional<bool>{fires};
	}

	bool RepeatsWhileHeld(const Chord& chord, bool commandRepeats)
	{
		return chord.GetFlags().RepeatsWhileHeld.value_or(commandRepeats);
	}

	void SetRepeatsWhileHeld(ChordFlags& flags, bool repeats, bool commandRepeats)
	{
		const bool matchesCommand = repeats == commandRepeats;
		flags.RepeatsWhileHeld = matchesCommand ? std::nullopt : std::optional<bool>{repeats};
	}
}
