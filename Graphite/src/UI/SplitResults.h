#pragma once

#include "Core/ImGui/IconButton.h"

#include <functional>

namespace Graphite {

	struct SplitResultsInput
	{
		bool HasSelection = false;
		float& ResultsHeight;
		const std::function<void()>& DrawResults;
		const std::function<void()>& DrawPreview;
		const char* ResultsChildId = nullptr;
		const char* PreviewChildId = nullptr;
		const char* SplitterId = nullptr;
	};

	inline constexpr float kDefaultResultsShare = 0.65f;

	float DefaultResultsHeight(float available, float divider);

	void DrawSplitResults(const SplitResultsInput& input);

	// At the pane's top right corner, on the line of the heading before it: Frame after a heading laid out to the
	// frame height, Line after a heading of plain text, which the button must not make taller.
	bool DrawPaneClose(Core::IconButtonFit fit = Core::IconButtonFit::Frame);

	// What a heading on the close button's line leaves for the button and the gap before it.
	float PaneCloseRoom(Core::IconButtonFit fit = Core::IconButtonFit::Frame);
}
