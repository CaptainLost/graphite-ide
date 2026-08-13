#pragma once

#include <imgui.h>

namespace Core {

	struct ModifierName
	{
		ImGuiKeyChord Bit;
		const char* Name;
	};

	constexpr ModifierName kModifiers[] = {
		{ImGuiMod_Ctrl, "Ctrl"},
		{ImGuiMod_Shift, "Shift"},
		{ImGuiMod_Alt, "Alt"},
		{ImGuiMod_Super, "Super"},
	};
}