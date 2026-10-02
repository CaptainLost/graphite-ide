#pragma once

#include <imgui.h>

#include <string>

namespace Core {

	// ImGui's text fields bound to a std::string that grows as it is typed into, rather than to a
	// fixed char buffer that silently cuts a longer value off - imgui_stdlib's shape, which this
	// build does not include. True when the text changed, as ImGui's own fields return.
	bool InputText(const char* label, std::string& text, ImGuiInputTextFlags flags = 0);

	bool InputTextWithHint(const char* label, const char* hint, std::string& text, ImGuiInputTextFlags flags = 0);

	bool InputTextMultiline(const char* label, std::string& text, const ImVec2& size = ImVec2(0.0f, 0.0f),
							ImGuiInputTextFlags flags = 0);
}
