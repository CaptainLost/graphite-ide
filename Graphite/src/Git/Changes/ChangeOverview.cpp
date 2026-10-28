#include "Git/Changes/ChangeOverview.h"

#include <algorithm>
#include <array>

namespace Graphite {

	namespace {

		OverviewMark MarkOf(const ChangeHunk& hunk, const std::function<float(std::size_t)>& lineTop,
							const std::function<float(std::size_t)>& lineEnd, float minFraction)
		{
			const float from = lineTop(hunk.FirstLine);
			const bool spansNoLine = hunk.Kind == HunkKind::Deleted || hunk.LineCount == 0;
			const float to = spansNoLine ? from : lineEnd(hunk.FirstLine + hunk.LineCount - 1);

			OverviewMark mark{.From = from,
							  .To = std::max(to, from + minFraction),
							  .Kind = hunk.Kind,
							  .Staged = hunk.Staged};

			// WARNING: The mark is pushed back up rather than cut short, so a change on the last
			// line keeps its height.
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
		return BuildChangeOverview(markers, lineTop, [&lineTop](std::size_t line) { return lineTop(line + 1); },
								   minFraction);
	}

	std::vector<OverviewMark> BuildChangeOverview(const ChangeMarkers& markers,
												  const std::function<float(std::size_t)>& lineTop,
												  const std::function<float(std::size_t)>& lineEnd, float minFraction)
	{
		std::vector<OverviewMark> marks;
		marks.reserve(markers.Hunks.size());

		constexpr std::array<bool, 2> kStagedFirst{true, false};

		for (const bool stagedPass : kStagedFirst)
		{
			for (const ChangeHunk& hunk : markers.Hunks)
			{
				if (hunk.Staged == stagedPass)
				{
					marks.push_back(MarkOf(hunk, lineTop, lineEnd, minFraction));
				}
			}
		}

		return marks;
	}
}
