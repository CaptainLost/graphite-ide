#pragma once

#include "Sessions/Overview/DashboardCanvas.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>
#include <vector>

namespace Graphite {

	inline constexpr std::size_t kTimelineLeastDays = 3;

	// WARNING: Tall enough for kTimelineLeastDays quiet days; the tile gets the height left below the others and shows
	// as many days as fit in it.
	float TimelineHeight(float width);

	struct TimelineRow
	{
		std::size_t Day = 0; // index into DashboardModel::Timeline
		float Top = 0.0f;
		float Height = 0.0f;
	};

	// WARNING: Measured at the current font size, in the coordinates the tile is given in. A row is as tall as its
	// day's lanes need, so how many days fit depends on the days as well as on the tile.
	struct TimelineGrid
	{
		float Left = 0.0f;	// the day labels' left edge
		float Right = 0.0f; // the day costs' right edge
		float TrackLeft = 0.0f;
		float TrackRight = 0.0f;
		float FirstRowTop = 0.0f;
		std::vector<TimelineRow> Rows; // today first

		float HourX(int hour) const;
	};

	TimelineGrid TimelineGridOf(const ImRect& tile, const DashboardModel& model);

	// WARNING: The lanes' band inside a row, centred in it; a bar in lane n is the nth slice of it.
	ImRect TimelineTrack(const TimelineGrid& grid, const TimelineRow& row, std::size_t lanes);

	void DrawTimeline(DashboardCanvas& canvas, const ImRect& tile);
}
