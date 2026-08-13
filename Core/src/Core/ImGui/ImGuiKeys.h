#pragma once

#include "Core/Base/KeyCodes.h"

#include <imgui.h>

namespace Core {

	// Bridges the key codes events carry to the ones bindings are stored in.
	//
	// Bindings are written to settings as text produced from ImGui's own key names, so the
	// stored representation stays ImGui's. Translating the event here rather than changing
	// what a Chord holds is what keeps every settings file written so far readable.
	ImGuiKey ToImGuiKey(KeyCode key);

	// The modifier bits an event carries, in the form a chord compares against.
	ImGuiKeyChord ToImGuiModifiers(KeyModifier modifiers);
}
