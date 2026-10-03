#include "Git/ChangeOverview.h"

#include <algorithm>

namespace Graphite {

	namespace {

		OverviewMark MarkOf(const ChangeHunk& hunk, const std::function<float(std::size_t)>& lineTop,
							float minFraction)
		{
			const float from = lineTop(hunk.FirstLine);
			const float to = hunk.Kind == HunkKind::Deleted ? from : lineTop(hunk.FirstLine + hunk.LineCount);

			OverviewMark mark{.From = from, .To = std::max(to, from + minFraction), .Kind = hunk.Kind, .Staged = hunk.Staged};

			// Pushed back up rather than cut short, so a change on the last line keeps its height.
			if (mark.To > 1.0f)
			{
				mark.From = std::max(0.0f, mark.From - (mark.To - 1.0f));
				mark.To = 1.0f;
			}

			return mark;
		}
	}

	std::vector<OverviewMark> BuildChangeOverview(const ChangeMarkers& markers,
												  const std::function<float(std::size_t)>& lineTop,
												  float minFraction)
	{
		std::vector<OverviewMark> marks;
		marks.reserve(markers.Hunks.size());

		for (const bool staged : {true, false})
		{
			for (const ChangeHunk& hunk : markers.Hunks)
			{
				if (hunk.Staged == staged)
					marks.push_back(MarkOf(hunk, lineTop, minFraction));
			}
		}

		return marks;
	}
}
