#pragma once

#include <imgui.h>

#include <string>
#include <string_view>

namespace Core {

	std::string NameOfKey(ImGuiKey key);

	ImGuiKey KeyNamed(std::string_view name);

	bool IsModifierKey(ImGuiKey key);

	// ImGui's named-key range also holds gamepad buttons, mouse aliases and the reserved
	// ReservedForModCtrl..ReservedForModSuper slots, which sit inside NamedKey_BEGIN..END far
	// from the modifier span. Answered as what a keyboard key is, not as a list of what to
	// skip, because such a list went stale twice.
	bool IsKeyboardKey(ImGuiKey key);
}