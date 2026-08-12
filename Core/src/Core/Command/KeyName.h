#pragma once

#include <imgui.h>

#include <string>
#include <string_view>

namespace Core {

	std::string NameOfKey(ImGuiKey key);

	ImGuiKey KeyNamed(std::string_view name);

	bool IsModifierKey(ImGuiKey key);

	bool IsKeyboardKey(ImGuiKey key);
}