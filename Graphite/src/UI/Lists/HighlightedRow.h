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

	// WARNING: The pieces are drawn as items side by side, letting ImGui advance between them, rather than as one string
	// with a rectangle behind the match: placing a rectangle means measuring the text, and CalcTextSize rounds up, which
	// drifts the highlight off the text.
	void DrawHighlightedLabel(const HighlightedLabel& label, const ImVec4& matchColour);
}
