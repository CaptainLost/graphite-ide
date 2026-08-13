#pragma once

#include "Core/Base/KeyCodes.h"

#include <imgui.h>

namespace Core {

	// Bridges the key codes events carry to the ones bindings are stored in. Settings hold
	// ImGui's own key names, so it is the event that is translated, not what a Chord holds.
	ImGuiKey ToImGuiKey(KeyCode key);

	// The modifier bits an event carries, in the form a chord compares against.
	ImGuiKeyChord ToImGuiModifiers(KeyModifier modifiers);

	// The bit a modifier key stands for, or zero when the key is not one. A key event does
	// not reliably report the modifier it is itself pressing, so the bit is taken from the
	// key rather than from the modifier mask beside it.
	ImGuiKeyChord ModifierBitOf(KeyCode key);
}
