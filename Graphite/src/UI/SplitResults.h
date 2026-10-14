#pragma once

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

	bool DrawPaneClose();
}
