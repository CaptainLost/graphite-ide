#pragma once

#include "Sessions/Overview/DashboardCanvas.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>

namespace Graphite {

	// WARNING: Measured at the current font size; an empty slot has a rectangle too, so rows never move.
	ImRect ListRowRect(const ImRect& tile, std::size_t row);

	float ListTileHeight(std::size_t slots);

	float ModelsHeight(float width);

	void DrawModels(DashboardCanvas& canvas, const ImRect& tile);

	void DrawProjects(DashboardCanvas& canvas, const ImRect& tile);

	void DrawSessions(DashboardCanvas& canvas, const ImRect& tile);

	void DrawTools(DashboardCanvas& canvas, const ImRect& tile);

	void DrawSkills(DashboardCanvas& canvas, const ImRect& tile);

	float CacheHeight(const DashboardModel& model, float width);

	void DrawCache(DashboardCanvas& canvas, const ImRect& tile);
}
