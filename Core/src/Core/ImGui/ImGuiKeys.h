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

	// The bit a modifier key stands for, or zero when the key is not one. A key event does
	// not reliably report the modifier it is itself pressing, so the bit is taken from the
	// key rather than from the modifier mask beside it.
	ImGuiKeyChord ModifierBitOf(KeyCode key);
}
