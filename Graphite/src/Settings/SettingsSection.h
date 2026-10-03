#pragma once

#include "Core/Base/NonCopyable.h"

#include <string_view>

namespace Core {

	class Event;
}

namespace Graphite {

	// One screen inside the settings panel. A section renders into a region the panel has
	// already established: it never calls ImGui::Begin, never knows whether it is docked, and
	// never knows whether it is visible.
	//
	// Deliberately not a Core::Panel. Panel carries window management — visibility, HasBeenDrawn,
	// its own Begin and End — and a section must have none of it; nesting one inside another
	// panel's body would nest ImGui windows.
	class SettingsSection : Core::NonCopyable
	{
	public:
		virtual ~SettingsSection() = default;

		virtual std::string_view GetTitle() const = 0;

		virtual void OnImGuiRender() = 0;

		// Only the section the user is looking at is offered events. A section that captures
		// keys — KeyBindingsSection does — must not capture them from behind another section.
		virtual void OnEvent(Core::Event&) {}

		// Called on the section being left, so a section holding transient state — a chord
		// capture, a half-filled field — can drop it rather than sit waiting for input it will
		// no longer be offered.
		virtual void OnDeactivated() {}
	};
}
