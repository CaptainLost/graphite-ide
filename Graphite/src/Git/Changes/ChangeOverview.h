#pragma once

#include "Git/Changes/ChangeMarkers.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace Graphite {

	struct OverviewMark
	{
		float From = 0.0f;
		float To = 0.0f;
		HunkKind Kind = HunkKind::Added;
		bool Staged = false;
	};

	// WARNING: lineTop(lineCount) is expected to be at most 1; a mark reaching past the end anyway,
	// as a stale hunk can for a frame, is pulled back inside. Staged marks come first, so drawing
	// in order leaves the unstaged change on top where the two overlap.
	std::vector<OverviewMark> BuildChangeOverview(const ChangeMarkers& markers,
												  const std::function<float(std::size_t)>& lineTop,
												  float minFraction);

	// lineEnd(line) is where the line's last row ends on the track. A mark ends there rather than at the next line's
	// top, which rows drawn between two lines, a peek's or a lens's, push further down.
	std::vector<OverviewMark> BuildChangeOverview(const ChangeMarkers& markers,
												  const std::function<float(std::size_t)>& lineTop,
												  const std::function<float(std::size_t)>& lineEnd, float minFraction);
}
