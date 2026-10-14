#pragma once

#include <imgui.h>

#include <string_view>
#include <vector>

namespace Core {

	struct CentredLine
	{
		std::string_view Text;
		ImVec2 Offset;
	};

	std::vector<CentredLine> CentreLines(std::string_view text, ImVec2 region, float below = 0.0f);

	bool EmptyState(std::string_view message, const char* action = nullptr, ImVec2 size = ImVec2(0.0f, 0.0f));
}
