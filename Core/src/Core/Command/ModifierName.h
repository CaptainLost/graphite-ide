#pragma once

#include <imgui.h>

namespace Core {

	struct ModifierName
	{
		ImGuiKeyChord bit;
		const char* name;
	};

	constexpr ModifierName kModifiers[] = {
		{ImGuiMod_Ctrl, "Ctrl"},
		{ImGuiMod_Shift, "Shift"},
		{ImGuiMod_Alt, "Alt"},
		{ImGuiMod_Super, "Super"},
	};
}