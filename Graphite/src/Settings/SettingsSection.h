#pragma once

#include <string_view>

namespace Core {

	class Event;
}

namespace Graphite {

	// One screen inside the settings panel. A section renders into a region the panel has
	// already established: it never calls ImGui::Begin, never knows whether it is docked, and
	// never knows whether it is visible.
	//
	// Deliberately not a Core::Panel. Panel carries window management — visibility, WasDrawn,
	// its own Begin and End — and a section must have none of it; nesting one inside another
	// panel's body would nest ImGui windows.
	class SettingsSection
	{
	public:
		virtual ~SettingsSection() = default;

		virtual std::string_view GetTitle() const = 0;

		virtual void OnImGuiRender() = 0;

		// Only the section the user is looking at is offered events. A section that captures
		// keys — KeyBindingsSection does — must not capture them from behind another section.
		virtual void OnEvent(Core::Event& event) { (void)event; }
	};
}
