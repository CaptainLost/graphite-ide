#pragma once

#include <imgui.h>

namespace Core {

	struct ModifierName
	{
		ImGuiKeyChord Bit;
		const char* Name;
	};

	constexpr ModifierName kModifiers[] = {
		{.Bit = ImGuiMod_Ctrl, .Name = "Ctrl"},
		{.Bit = ImGuiMod_Shift, .Name = "Shift"},
		{.Bit = ImGuiMod_Alt, .Name = "Alt"},
		{.Bit = ImGuiMod_Super, .Name = "Super"},
	};
}
