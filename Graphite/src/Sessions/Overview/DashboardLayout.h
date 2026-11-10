#pragma once

#include "Sessions/Overview/DashboardModel.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace Graphite {

	inline constexpr float kWideDashboardInFontSizes = 64.0f;	// >= : 12-column grid S
	inline constexpr float kMediumDashboardInFontSizes = 40.0f; // >= : two columns; below: one
	inline constexpr int kHeatWeeks = 13;
	inline constexpr std::size_t kTopRows = 7;	// projects, expensive sessions
	inline constexpr std::size_t kListRows = 8; // tools, skills

	inline constexpr int kDaysPerWeek = 7;
	inline constexpr std::size_t kMapDayCount = static_cast<std::size_t>(kHeatWeeks * kDaysPerWeek);
	inline constexpr std::size_t kNumberTileCount = 8;

	inline constexpr float kTileGapInFontSizes = 0.45f;
	inline constexpr float kTilePaddingYInFontSizes = 0.55f;
	inline constexpr float kTilePaddingXInFontSizes = 0.7f;

	enum class DashboardArea : std::uint8_t
	{
		Map,
		Daily,
		Models,
		Projects,
		Sessions,
		Tools,
		Skills,
		Punch,
		Cache,
		Count
	};

	inline constexpr std::size_t kDashboardAreaCount = static_cast<std::size_t>(DashboardArea::Count);

	struct DashboardGrid
	{
		std::array<ImRect, kNumberTileCount> Numbers;
		std::array<ImRect, kDashboardAreaCount> Areas;
		float Height = 0.0f;
	};

	using TileHeight = std::function<float(DashboardArea area, float width)>;

	DashboardGrid PlaceTiles(float width, float unit, float numberTileHeight, const TileHeight& heightOf);

	enum class MapDayKind : std::uint8_t
	{
		Future,
		Gone,
		OnDisk
	};

	struct MapDay
	{
		std::chrono::year_month_day Date;
		MapDayKind Kind = MapDayKind::Future;
		std::optional<std::size_t> Index; // into DashboardModel::Days when on disk
		int Level = 0;
	};

	struct MapDays
	{
		std::chrono::year_month_day FirstMonday;
		std::array<MapDay, kMapDayCount> Days;
	};

	MapDays MapDaysOf(const DashboardModel& model);
}
