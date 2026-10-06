#include "corepch.h"

#include "Core/Command/ShortcutRouter.h"

#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/ImGui/ImGuiKeys.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace Core {

	namespace {
		using ChordMatcher = std::function<bool(const Chord&)>;

		constexpr float kFewestNotchesOfAGesture = 1.0f;

		bool HasMatchingChord(const Binding& binding, const ChordMatcher& matchesChord)
		{
			if (matchesChord(binding.Primary))
			{
				return true;
			}

			for (const Chord& alternate : binding.Alternates)
			{
				if (matchesChord(alternate))
				{
					return true;
				}
			}

			return false;
		}

		// Collected first and run afterwards, because a command run inside the walk could change
		// the keymap being iterated, as registering a command with a default binding does.
		std::vector<std::string> CommandIdsBoundTo(const Keymap& keymap, const ChordMatcher& matchesChord)
		{
			std::vector<std::string> commandIds;

			keymap.ForEachBinding([&commandIds, &matchesChord](const std::string& commandId, const Binding& binding) {
				if (HasMatchingChord(binding, matchesChord))
				{
					commandIds.push_back(commandId);
				}
			});

			return commandIds;
		}

		void RunAll(CommandRegistry& commands, const std::vector<std::string>& commandIds, int runsPerCommand = 1)
		{
			for (const std::string& commandId : commandIds)
			{
				for (int runIndex = 0; runIndex < runsPerCommand; ++runIndex)
				{
					commands.Run(commandId);
				}
			}
		}

		// A command whose every chord was taken away holds an empty primary, and an empty chord
		// names no key: matching it would fire on the modifiers alone.
		bool IsChordForKeyPress(const Chord& chord, ImGuiKeyChord pressedKeys)
		{
			const bool chordIsEmpty = chord.IsEmpty();
			const bool firesOnKey = chord.GetTrigger() == Trigger::Key;
			const bool keysMatch = chord.GetKeys() == pressedKeys;

			return !chordIsEmpty && firesOnKey && keysMatch;
		}

		bool IsChordForModifierTap(const Chord& chord, ImGuiKeyChord tappedModifiers)
		{
			const bool firesOnTap = chord.GetTrigger() == Trigger::ModifierTap;
			const bool chordIsEmpty = chord.IsEmpty();
			const bool modifiersMatch = chord.GetKeys() == tappedModifiers;

			return firesOnTap && !chordIsEmpty && modifiersMatch;
		}

		bool IsChordForWheelTurn(const Chord& chord, Trigger direction, ShortcutScope scopeUnderPointer,
								 ImGuiKeyChord heldModifiers)
		{
			const bool firesOnDirection = chord.GetTrigger() == direction;
			const bool scopeMatches = chord.GetScope() == scopeUnderPointer;
			const bool modifiersMatch = chord.GetKeys() == heldModifiers;

			return firesOnDirection && scopeMatches && modifiersMatch;
		}

		bool IsSkippedForStroke(const CommandRegistry& commands, const std::string& commandId,
								ShortcutScope routedScope, bool strokeIsRepeat)
		{
			const Command* command = commands.Find(commandId);
			const bool commandIsRegistered = command != nullptr;
			const bool commandRepeats = !commandIsRegistered || command->RepeatsWhileHeld;

			const ShortcutScope commandScope = commands.KeyScopeOf(commandId);
			const bool firesInAnotherScope = commandScope != routedScope;
			const bool isUnwantedRepeat = strokeIsRepeat && !commandRepeats;

			return firesInAnotherScope || isUnwantedRepeat;
		}
	}

	bool ShortcutRouter::OnKeyPressed(CommandRegistry& commands, const Keymap& keymap,
									  ShortcutScope routedScope, const KeyPressedEvent& event)
	{
		const ImGuiKey pressedKey = ToImGuiKey(event.GetKeyCode());

		// Any key but a modifier cancels the tap, so releasing Ctrl after an abandoned Ctrl+S
		// does not fire what Ctrl alone is bound to. Only a Global stroke counts: the focused
		// editor replays strokes the layer already routed as Global, and counting a replayed
		// Ctrl again would re-arm the tap the S of that Ctrl+S had just abandoned.
		const bool strokeCountsTowardTap = routedScope == ShortcutScope::Global;
		const bool strokeIsRepeat = event.IsRepeat();

		const ImGuiKeyChord modifierBit = ModifierBitOf(event.GetKeyCode());
		const bool pressedAModifier = modifierBit != ImGuiMod_None;

		if (pressedAModifier)
		{
			if (strokeCountsTowardTap && !strokeIsRepeat)
			{
				m_PendingTapModifiers |= modifierBit;
			}

			return false;
		}

		if (strokeCountsTowardTap)
		{
			CancelTap();
		}

		if (pressedKey == ImGuiKey_None)
		{
			return false;
		}

		const ImGuiKeyChord pressedKeys = ToImGuiModifiers(event.GetModifiers()) | pressedKey;

		std::vector<std::string> commandIdsToRun =
			CommandIdsBoundTo(keymap, [pressedKeys](const Chord& chord) { return IsChordForKeyPress(chord, pressedKeys); });

		std::erase_if(commandIdsToRun, [&commands, routedScope, strokeIsRepeat](const std::string& commandId) {
			return IsSkippedForStroke(commands, commandId, routedScope, strokeIsRepeat);
		});

		RunAll(commands, commandIdsToRun);

		const bool eventWasHandled = !commandIdsToRun.empty();
		return eventWasHandled;
	}

	bool ShortcutRouter::OnKeyReleased(CommandRegistry& commands, const Keymap& keymap,
									   const KeyReleasedEvent& event)
	{
		const bool releasedAModifier = ModifierBitOf(event.GetKeyCode()) != ImGuiMod_None;
		const bool tapIsPending = m_PendingTapModifiers != ImGuiMod_None;

		if (!releasedAModifier || !tapIsPending)
		{
			return false;
		}

		// Read and cleared before anything runs: the command about to fire may open a
		// window or take the focus, and a tap left half-recorded would fire again on the
		// next modifier released anywhere.
		const ImGuiKeyChord tappedModifiers = m_PendingTapModifiers;
		m_PendingTapModifiers = ImGuiMod_None;

		const std::vector<std::string> commandIdsToRun = CommandIdsBoundTo(
			keymap, [tappedModifiers](const Chord& chord) { return IsChordForModifierTap(chord, tappedModifiers); });

		RunAll(commands, commandIdsToRun);

		const bool eventWasHandled = !commandIdsToRun.empty();
		return eventWasHandled;
	}

	bool ShortcutRouter::OnMouseScrolled(CommandRegistry& commands, const Keymap& keymap, ShortcutScope scopeUnderPointer,
										 const MouseScrolledEvent& event)
	{
		const float wheelOffset = event.GetYOffset();

		if (wheelOffset == 0.0f)
		{
			return false;
		}

		const Trigger direction = wheelOffset > 0.0f ? Trigger::WheelUp : Trigger::WheelDown;

		// A fast spin reports several notches in one event, so firing once per event would make
		// a wheel-bound command visibly slower.
		const float roundedNotches = std::round(std::fabs(wheelOffset));
		const auto notchCount = static_cast<int>(std::max(kFewestNotchesOfAGesture, roundedNotches));

		const ImGuiKeyChord heldModifiers = ImGui::GetIO().KeyMods;

		const std::vector<std::string> commandIdsToRun =
			CommandIdsBoundTo(keymap, [direction, scopeUnderPointer, heldModifiers](const Chord& chord) {
				return IsChordForWheelTurn(chord, direction, scopeUnderPointer, heldModifiers);
			});

		RunAll(commands, commandIdsToRun, notchCount);

		const bool eventWasHandled = !commandIdsToRun.empty();
		return eventWasHandled;
	}
}
