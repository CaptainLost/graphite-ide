#pragma once

#include "Git/History/HistoryStatus.h"

#include <optional>
#include <string_view>

namespace Graphite {

	enum class HistoryClick
	{
		// Load 500 more, or Search further.
		More,
		// Load all, or Search all.
		All,
		// Fold them: the line's newer commits go back into one row.
		Fold
	};

	// The footer's height, so the list above can leave exactly that much room for it. The footer is drawn straight after
	// the list and starts where the list ends.
	float HistoryStatusLineHeight();

	// The text, then `foldFooter` and its "Fold them" link when it is given, then either the busy loader or the pair of
	// buttons against the right edge. Answers what was clicked.
	std::optional<HistoryClick> DrawHistoryStatusLine(const HistoryStatus& status, std::string_view foldFooter = {});
}
