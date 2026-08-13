#include "corepch.h"

#include "Core/Command/ShortcutRouter.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/ImGui/ImGuiKeys.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Core {

	namespace {
		// Collected first, run afterwards, throughout this file: running a command inside the
		// walk would let it register another command while the map is being iterated.
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
									  const KeyPressedEvent& event)
	{
		const ImGuiKey key = ToImGuiKey(event.GetKeyCode());

		if (key == ImGuiKey_None)
		{
			return false;
		}

		const ImGuiKeyChord pressed = ToImGuiModifiers(event.GetModifiers()) | key;
		std::vector<std::string> fired;

		keys.ForEachBinding(
			[&fired, pressed](const std::string& id, const Binding& binding) {
				const auto matches = [pressed](const Chord& chord) {
					// A command every chord was taken away from holds an empty primary, and an
					// empty chord names no key: matching it would fire on the modifiers alone.
					return !chord.IsEmpty() && chord.GetTrigger() == Trigger::Key &&
						   chord.GetScope() == Chord::Scope::Global && chord.GetKeys() == pressed;
				};

				if (matches(binding.Primary))
				{
					fired.push_back(id);
					return;
				}

				for (const Chord& alternate : binding.Alternates)
				{
					if (matches(alternate))
					{
						fired.push_back(id);
						return;
					}
				}
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

		// A fast spin reports several notches at once, and the zoom this replaces added the
		// raw value — so one notch per event would make wheel zoom visibly slower. At least
		// one, since any non-zero wheel is a gesture the user made.
		const auto notches = static_cast<int>(std::max(1.0f, std::round(std::fabs(wheel))));

		const ImGuiKeyChord mods = ToImGuiModifiers(KeyModifier::None) | ImGui::GetIO().KeyMods;
		std::vector<std::string> fired;

		keys.ForEachBinding(
			[&fired, scope, direction, mods](const std::string& id, const Binding& binding) {
				const auto matches = [&](const Chord& chord) {
					return chord.GetTrigger() == direction && chord.GetScope() == scope &&
						   chord.GetKeys() == mods;
				};

				if (matches(binding.Primary))
				{
					fired.push_back(id);
					return;
				}

				for (const Chord& alternate : binding.Alternates)
				{
					if (matches(alternate))
					{
						fired.push_back(id);
						return;
					}
				}
			});

		RunAll(commands, fired, notches);
		return !fired.empty();
	}
}
