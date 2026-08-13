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

		std::vector<std::string> fired;

		keys.ForEachBinding(
			[&fired, tapped](const std::string& id, const Binding& binding) {
				const auto matches = [tapped](const Chord& chord) {
					return chord.GetTrigger() == Trigger::ModifierTap && !chord.IsEmpty() &&
						   chord.GetKeys() == tapped;
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
