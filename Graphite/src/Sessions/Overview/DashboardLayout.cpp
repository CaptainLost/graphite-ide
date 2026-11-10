#include "Sessions/Overview/DashboardLayout.h"

#include "Sessions/Overview/DashboardFigures.h"

#include <algorithm>
#include <vector>

namespace Graphite {

	namespace {
		constexpr int kWideColumns = 12;
		constexpr int kMediumColumns = 2;
		constexpr int kNarrowColumns = 1;
		constexpr std::size_t kWideNumbersPerRow = 8;
		constexpr std::size_t kMediumNumbersPerRow = 4;
		constexpr std::size_t kNarrowNumbersPerRow = 2;

		struct Span
		{
			DashboardArea Area;
			int Columns = 1;
		};

		using Row = std::vector<Span>;

		std::vector<Row> WideRows()
		{
			constexpr int kMap = 4;
			constexpr int kDaily = 5;
			constexpr int kModels = 3;
			constexpr int kProjects = 4;
			constexpr int kSessions = 4;
			constexpr int kTools = 2;
			constexpr int kSkills = 2;
			constexpr int kPunch = 7;
			constexpr int kCache = 5;

			return {{{DashboardArea::Map, kMap}, {DashboardArea::Daily, kDaily}, {DashboardArea::Models, kModels}},
					{{DashboardArea::Projects, kProjects},
					 {DashboardArea::Sessions, kSessions},
					 {DashboardArea::Tools, kTools},
					 {DashboardArea::Skills, kSkills}},
					{{DashboardArea::Punch, kPunch}, {DashboardArea::Cache, kCache}}};
		}

		std::vector<Row> MediumRows()
		{
			return {{{DashboardArea::Map}, {DashboardArea::Daily}},
					{{DashboardArea::Models}, {DashboardArea::Projects}},
					{{DashboardArea::Sessions}, {DashboardArea::Tools}},
					{{DashboardArea::Skills}, {DashboardArea::Punch}},
					{{DashboardArea::Cache, kMediumColumns}}};
		}

		std::vector<Row> NarrowRows()
		{
			std::vector<Row> rows;
			for (std::size_t area = 0; area < kDashboardAreaCount; ++area)
			{
				rows.push_back({{static_cast<DashboardArea>(area)}});
			}

			return rows;
		}

		float SpanWidth(int columns, float columnWidth, float gap)
		{
			return static_cast<float>(columns) * columnWidth + static_cast<float>(columns - 1) * gap;
		}
	}

	DashboardGrid PlaceTiles(float width, float unit, float numberTileHeight, const TileHeight& heightOf)
	{
		const float gap = kTileGapInFontSizes * unit;
		const float widthInFontSizes = unit > 0.0f ? width / unit : 0.0f;
		const bool wide = widthInFontSizes >= kWideDashboardInFontSizes;
		const bool medium = !wide && widthInFontSizes >= kMediumDashboardInFontSizes;

		const int columns = wide ? kWideColumns : medium ? kMediumColumns : kNarrowColumns;
		const std::size_t numbersPerRow = wide ? kWideNumbersPerRow : medium ? kMediumNumbersPerRow : kNarrowNumbersPerRow;
		const std::vector<Row> rows = wide ? WideRows() : medium ? MediumRows() : NarrowRows();

		const float left = gap;
		const float inner = std::max(0.0f, width - 2.0f * gap);
		DashboardGrid grid;
		float top = gap;

		const float numberWidth =
			std::max(0.0f, (inner - static_cast<float>(numbersPerRow - 1) * gap) / static_cast<float>(numbersPerRow));

		for (std::size_t tile = 0; tile < kNumberTileCount; ++tile)
		{
			const std::size_t column = tile % numbersPerRow;
			const std::size_t row = tile / numbersPerRow;
			const ImVec2 min(left + static_cast<float>(column) * (numberWidth + gap),
							 top + static_cast<float>(row) * (numberTileHeight + gap));
			grid.Numbers[tile] = ImRect(min, ImVec2(min.x + numberWidth, min.y + numberTileHeight));
		}

		const std::size_t numberRows = (kNumberTileCount + numbersPerRow - 1) / numbersPerRow;
		top += static_cast<float>(numberRows) * (numberTileHeight + gap);

		const float columnWidth =
			std::max(0.0f, (inner - static_cast<float>(columns - 1) * gap) / static_cast<float>(columns));

		for (const Row& row : rows)
		{
			float rowHeight = 0.0f;
			for (const Span& span : row)
			{
				rowHeight = std::max(rowHeight, heightOf(span.Area, SpanWidth(span.Columns, columnWidth, gap)));
			}

			int column = 0;
			for (const Span& span : row)
			{
				const float x = left + static_cast<float>(column) * (columnWidth + gap);
				grid.Areas[static_cast<std::size_t>(span.Area)] =
					ImRect(ImVec2(x, top), ImVec2(x + SpanWidth(span.Columns, columnWidth, gap), top + rowHeight));
				column += span.Columns;
			}

			top += rowHeight + gap;
		}

		grid.Height = top;
		return grid;
	}

	MapDays MapDaysOf(const DashboardModel& model)
	{
		using std::chrono::days;
		using std::chrono::sys_days;

		const sys_days today{model.Today};
		const std::chrono::weekday weekday{today};
		const sys_days thisMonday = today - days{weekday.iso_encoding() - 1};
		const sys_days firstMonday = thisMonday - days{static_cast<long long>(kDaysPerWeek) * (kHeatWeeks - 1)};
		const std::optional<sys_days> oldest =
			model.Days.empty() ? std::nullopt : std::optional<sys_days>{sys_days{model.Days.front().Day}};
		const std::array<double, 3> cuts = HeatCuts(model);
		const auto costliest = std::ranges::max_element(model.Days, {}, &DayTotals::Cost);
		const double highest = costliest == model.Days.end() ? 0.0 : costliest->Cost;

		MapDays map{.FirstMonday = std::chrono::year_month_day{firstMonday}, .Days = {}};

		for (std::size_t slot = 0; slot < kMapDayCount; ++slot)
		{
			const sys_days date = firstMonday + days{static_cast<long long>(slot)};
			MapDay& day = map.Days[slot];
			day.Date = std::chrono::year_month_day{date};

			if (date > today)
			{
				day.Kind = MapDayKind::Future;
			}
			else if (!oldest || date < *oldest)
			{
				day.Kind = MapDayKind::Gone;
			}
			else
			{
				day.Kind = MapDayKind::OnDisk;
				day.Index = DayIndex(model, day.Date);
				day.Level = HeatLevel(day.Index ? model.Days[*day.Index].Cost : 0.0, cuts, highest);
			}
		}

		return map;
	}
}
