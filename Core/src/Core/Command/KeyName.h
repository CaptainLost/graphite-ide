#pragma once

#include <imgui.h>

#include <string>
#include <string_view>

namespace Core {

	std::string NameOfKey(ImGuiKey key);

	ImGuiKey KeyNamed(std::string_view name);

	bool IsModifierKey(ImGuiKey key);

	// Says what a keyboard key is, rather than listing what to skip - the skip list went
	// stale twice. ImGui's named-key range is four blocks, and the reserved
	// ReservedForModCtrl..ReservedForModSuper slots sit inside NamedKey_BEGIN..END, far
	// from the modifier span. Missing them recorded Ctrl+ModCtrl; and since arming a
	// capture is a left click, the capture answered itself.
	bool IsKeyboardKey(ImGuiKey key);
}