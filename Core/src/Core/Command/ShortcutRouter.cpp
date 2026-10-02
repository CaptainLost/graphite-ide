#include "corepch.h"

#include "Core/Command/ShortcutRouter.h"

#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/ImGui/ImGuiKeys.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Core {

	namespace {
		// Collected first and run afterwards, because running a command inside the walk would
		// let it register another command while the keymap is being iterated.
		template<typename Matches>
		std::vector<std::string> CommandsBoundTo(const Keymap& keys, Matches matches)
		{
			std::vector<std::string> fired;

			keys.ForEachBinding([&fired, &matches](const std::string& id, const Binding& binding) {
				if (matches(binding.Primary) ||
					std::any_of(binding.Alternates.begin(), binding.Alternates.end(), matches))
				{
					fired.push_back(id);
				}
			});

			return fired;
		}

		void RunAll(CommandRegistry& commands, const std::vector<std::string>& fired, int times = 1)
		{
			for (const std::string& id : fired)
			{
				for (int repeat = 0; repeat < times; ++repeat)
				{
					commands.Run(id);
				}
			}
		}
	}

	bool ShortcutRouter::OnKeyPressed(CommandRegistry& commands, const Keymap& keys,
									  Chord::Scope scope, const KeyPressedEvent& event)
	{
		const ImGuiKey key = ToImGuiKey(event.GetKeyCode());

		// A modifier going down keeps a tap possible and adds itself to it; anything else
		// going down ends the possibility, which is what stops an abandoned Ctrl+S from
		// firing whatever Ctrl alone is bound to when the hand comes off.
		if (const ImGuiKeyChord bit = ModifierBitOf(event.GetKeyCode()); bit != 0)
		{
			if (!event.IsRepeat())
			{
				m_TapMods |= bit;
			}

			return false;
		}

		m_TapMods = 0;

		if (key == ImGuiKey_None)
		{
			return false;
		}

		const ImGuiKeyChord pressed = ToImGuiModifiers(event.GetModifiers()) | key;

		std::vector<std::string> fired =
			CommandsBoundTo(keys, [pressed](const Chord& chord) {
				// A command every chord was taken away from holds an empty primary, and an
				// empty chord names no key: matching it would fire on the modifiers alone.
				return !chord.IsEmpty() && chord.GetTrigger() == Trigger::Key &&
					   chord.GetKeys() == pressed;
			});

		// A binding whose command was never registered is treated as Global, the scope every
		// chord had before commands carried one.
		std::erase_if(fired, [&commands, scope, &event](const std::string& id) {
			const Command* command = commands.Find(id);
			const Chord::Scope wanted = command != nullptr ? command->KeyScope : Chord::Scope::Global;
			const bool repeats = command == nullptr || command->Repeats;

			return wanted != scope || (event.IsRepeat() && !repeats);
		});

		RunAll(commands, fired);
		return !fired.empty();
	}

	bool ShortcutRouter::OnKeyReleased(CommandRegistry& commands, const Keymap& keys,
									   const KeyReleasedEvent& event)
	{
		if (ModifierBitOf(event.GetKeyCode()) == 0 || m_TapMods == 0)
		{
			return false;
		}

		// Read and cleared before anything runs: the command about to fire may open a
		// window or take the focus, and a tap left half-recorded would fire again on the
		// next modifier released anywhere.
		const ImGuiKeyChord tapped = m_TapMods;
		m_TapMods = 0;

		const std::vector<std::string> fired =
			CommandsBoundTo(keys, [tapped](const Chord& chord) {
				return chord.GetTrigger() == Trigger::ModifierTap && !chord.IsEmpty() &&
					   chord.GetKeys() == tapped;
			});

		RunAll(commands, fired);
		return !fired.empty();
	}

	bool ShortcutRouter::OnMouseScrolled(CommandRegistry& commands, const Keymap& keys, Chord::Scope scope,
										 const MouseScrolledEvent& event)
	{
		const float wheel = event.GetYOffset();

		if (wheel == 0.0f)
		{
			return false;
		}

		const Trigger direction = wheel > 0.0f ? Trigger::WheelUp : Trigger::WheelDown;

		// A fast spin reports several notches at once, so firing once per event would make a
		// wheel-bound command visibly slower. At least one, since any non-zero wheel is a
		// gesture the user made.
		const auto notches = static_cast<int>(std::max(1.0f, std::round(std::fabs(wheel))));

		const ImGuiKeyChord mods = ImGui::GetIO().KeyMods;

		const std::vector<std::string> fired =
			CommandsBoundTo(keys, [scope, direction, mods](const Chord& chord) {
				return chord.GetTrigger() == direction && chord.GetScope() == scope &&
					   chord.GetKeys() == mods;
			});

		RunAll(commands, fired, notches);
		return !fired.empty();
	}
}
