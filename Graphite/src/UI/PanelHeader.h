#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Graphite {

	inline constexpr float kPanelHeaderHeight = 36.0f;
	inline constexpr std::size_t kPanelHeaderMostChoices = 2;

	struct PanelHeaderAction
	{
		const char* Id;
		const char* Glyph;
		const char* Tooltip;
	};

	struct PanelHeaderChoice
	{
		const char* Id;
		std::vector<std::string> Options;
		int Selected;
	};

	struct PanelHeaderResult
	{
		std::array<std::optional<int>, kPanelHeaderMostChoices> Chosen;
		std::optional<std::size_t> ClickedAction;
	};

	float PanelHeaderHeight();

	PanelHeaderResult DrawPanelHeader(std::span<const PanelHeaderChoice> choices,
									  std::span<const PanelHeaderAction> actions);
}
