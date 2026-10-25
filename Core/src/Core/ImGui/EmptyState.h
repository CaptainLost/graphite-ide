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

	// WARNING: With a note, the action's button drawn disabled says it in a tooltip and still answers a click, as
	// OfferWhileDisabled does.
	bool EmptyState(std::string_view message, const char* action = nullptr, ImVec2 size = ImVec2(0.0f, 0.0f),
					const char* noteWhileDisabled = nullptr);
}
