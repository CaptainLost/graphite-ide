#pragma once

#include "Sessions/Overview/DashboardCanvas.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>
#include <string>
#include <string_view>

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

	struct CacheSentence
	{
		std::string Before;
		std::string Without; // drawn strong
		std::string After;

		std::string Whole() const { return Before + Without + After; }
	};

	CacheSentence CacheSentenceOf(std::string_view without, std::string_view actual, bool lost);

	// WARNING: Worded the other way round when the cache cost more than it saved, so the sentence never claims a saving.
	CacheSentence CacheSentenceOf(const DashboardModel& model);

	float CacheHeight(float width);

	void DrawCache(DashboardCanvas& canvas, const ImRect& tile);
}
