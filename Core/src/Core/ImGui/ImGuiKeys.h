#pragma once

#include "Core/Base/KeyCodes.h"

#include <imgui.h>

namespace Core {

	ImGuiKey ToImGuiKey(KeyCode key);

	ImGuiKeyChord ToImGuiModifiers(KeyModifier modifiers);

	// WARNING: a key event does not reliably report the modifier it is itself pressing, so
	// the bit comes from the key, not from the modifier mask beside it.
	ImGuiKeyChord ModifierBitOf(KeyCode key);
}
