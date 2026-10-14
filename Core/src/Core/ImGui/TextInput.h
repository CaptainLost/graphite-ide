#pragma once

#include <imgui.h>

#include <string>

namespace Core {

	bool InputText(const char* label, std::string& text, ImGuiInputTextFlags flags = 0);

	bool InputTextWithHint(const char* label, const char* hint, std::string& text, ImGuiInputTextFlags flags = 0);

	bool InputTextMultiline(const char* label, std::string& text, const ImVec2& size = ImVec2(0.0f, 0.0f),
							ImGuiInputTextFlags flags = 0);
}
