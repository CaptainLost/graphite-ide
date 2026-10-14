#pragma once

#include <imgui.h>

#include <string>
#include <string_view>

namespace Graphite {

	struct HighlightedLabel
	{
		std::string_view Before;
		std::string_view Match;
		std::string_view After;
	};

	HighlightedLabel SplitHighlight(std::string_view label, std::string_view needle);

	// WARNING: a temporary label would return views into a string destroyed before they are drawn.
	HighlightedLabel SplitHighlight(std::string&&, std::string_view) = delete;

	bool MarkedSelectable(const char* id, bool selected, std::string_view marker,
						  ImU32 markerColor, std::string_view text,
						  std::string_view needle = {});

	bool HighlightedSelectable(const char* id, bool selected, std::string_view label,
							   std::string_view needle);
}
