#pragma once

#include "Git/Changes/ChangeMarkers.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace Graphite {

	// One change as the scrollbar shows it: a stretch of the track, as fractions of its height
	// from the top.
	struct OverviewMark
	{
		float From = 0.0f;
		float To = 0.0f;
		HunkKind Kind = HunkKind::Added;
		bool Staged = false;
	};

	// Where each hunk of `markers` falls on the scrollbar. `lineTop` answers where a line's top
	// sits, as a fraction of the height the scrollbar spans; the caller's layout decides that,
	// wrapped rows and lens rows included. `lineTop(lineCount)` is expected to be at most 1; a
	// mark that reaches past the end anyway, as a stale hunk can for a frame, is pulled back
	// inside. A mark is at least `minFraction` tall, so one line in a long file stays visible.
	//
	// Staged marks come first, so drawing in order leaves the unstaged change on top where the
	// two overlap: the same precedence the gutter gives it.
	std::vector<OverviewMark> BuildChangeOverview(const ChangeMarkers& markers,
												  const std::function<float(std::size_t)>& lineTop,
												  float minFraction);
}
