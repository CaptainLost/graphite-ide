#pragma once

#include "Git/History/HistoryStatus.h"

#include <optional>

namespace Graphite {

	enum class HistoryClick
	{
		// Load 500 more, or Search further.
		More,
		// Load all, or Search all.
		All
	};

	// One text line high, so the list above can leave exactly that much room for it.
	float HistoryStatusLineHeight();

	// The text, then either the busy loader or the pair of buttons against the right edge.
	// `busySince` holds when the busy wait began, for the loader's delay. Answers the button clicked.
	std::optional<HistoryClick> DrawHistoryStatusLine(const HistoryStatus& status, std::optional<double>& busySince);
}
