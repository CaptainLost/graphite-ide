#pragma once

#include "Sessions/Overview/DashboardCanvas.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace Graphite {

	struct WeekChange
	{
		std::optional<bool> Rising; // none when the rounded change is nought
		std::string Percent;		// "12%"
	};

	std::optional<WeekChange> ThisWeekChange(const DashboardModel& model); // none when nothing was spent the same days last week

	float NumberTileHeight(float unit);

	void DrawNumberTile(DashboardCanvas& canvas, const ImRect& tile, NumberTile which);

	void DrawSparkline(ImDrawList& drawing, const ImRect& box, std::span<const double> values, ImU32 colour,
					   std::optional<std::size_t> marker = std::nullopt, ImU32 markerColour = 0);

	// WARNING: Measured at the current font size; cell (week, weekday) starts at FirstCell plus that many pitches.
	struct MapGrid
	{
		ImVec2 FirstCell;
		float Pitch = 0.0f;
		float Cell = 0.0f;
	};

	MapGrid MapGridOf(const ImRect& tile);

	float MapHeight(const DashboardModel& model, float width);

	void DrawMap(DashboardCanvas& canvas, const ImRect& tile);

	float DailyHeight(float width);

	void DrawDaily(DashboardCanvas& canvas, const ImRect& tile);

	float PunchHeight(float width);

	void DrawPunch(DashboardCanvas& canvas, const ImRect& tile);
}
