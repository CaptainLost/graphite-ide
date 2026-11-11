#include "Sessions/Overview/TimelineTile.h"

#include "Sessions/Overview/DashboardFigures.h"
#include "Sessions/Overview/TipText.h"
#include "UI/TypographicMarks.h"

#include "Core/ImGui/ImGuiColors.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {
		using Date = std::chrono::year_month_day;
		using DashboardDrawing::kHalf;

		static_assert(kTimelineMostDays == kMapDayCount, "the timeline reaches back as far as the map");

		constexpr int kHoursPerDay = 24;
		constexpr int kHourLabelEvery = 3;
		constexpr int kQuarterDay = 6;

		// WARNING: Lengths are in font sizes; each is multiplied by the unit before it is drawn.
		constexpr float kHourRow = 0.9f;
		constexpr float kHourLabelSize = 0.7f;
		constexpr float kLeastRow = 1.17f;
		constexpr float kRowPadding = 0.3f; // above and below the lanes together
		constexpr float kLane = 0.5f;
		constexpr float kLaneGap = 0.12f;
		constexpr float kBarGap = 0.08f;
		constexpr float kBarRounding = 0.12f;
		constexpr float kColumnGap = 0.6f;
		constexpr float kHatchSpacing = 0.35f;
		constexpr std::string_view kWidestDay = "Wed 00 Mmm";
		constexpr std::string_view kWidestCost = "$0000.00 ?";

		// By the quarter of the shown bars' costs per hour a bar falls in, so the dearer hours stand out on any theme.
		constexpr std::array<float, 4> kBarAlphas = {0.35f, 0.55f, 0.78f, 1.0f};

		ImRect Content(const ImRect& tile, float unit)
		{
			return ImRect(ImVec2(tile.Min.x + TileSidePadding(unit), tile.Min.y + TileHeaderHeight(unit)),
						  ImVec2(tile.Max.x - TileSidePadding(unit), tile.Max.y - TileBottomPadding(unit)));
		}

		float LanesHeight(std::size_t lanes, float unit)
		{
			const auto count = static_cast<float>(std::max<std::size_t>(1, lanes));
			return (count * kLane + (count - 1.0f) * kLaneGap) * unit;
		}

		float RowHeight(std::size_t lanes, float unit)
		{
			return std::max(kLeastRow * unit, LanesHeight(lanes, unit) + kRowPadding * unit);
		}

		std::string DayCost(const DayTotals& day)
		{
			if (day.Cost <= 0.0 && day.Requests == 0)
			{
				return std::string{kEnDash};
			}

			return day.UnpricedModels.empty() ? TipMoney(day.Cost) : TipMoney(day.Cost) + " " + std::string{kUnknownAmount};
		}

		std::string BarBanner(Date date, const TimelineBar& bar)
		{
			const std::string money = bar.Unpriced ? TipMoney(bar.Cost) + " " + std::string{kUnknownAmount} : TipMoney(bar.Cost);
			return std::format("{}, {:02}:00{}{:02}:00: {}", WeekdayDate(date), bar.FirstHour, kEnDash, bar.EndHour, money);
		}

		double CostPerHour(const TimelineBar& bar)
		{
			return bar.Cost / static_cast<double>(std::max(1, bar.EndHour - bar.FirstHour));
		}

		// The costs per hour at the quarter points of the shown bars that cost anything, by nearest rank.
		std::array<double, 3> QuarterCuts(const DashboardModel& model, std::span<const TimelineRow> rows)
		{
			std::vector<double> costs;

			for (const TimelineRow& row : rows)
			{
				for (const TimelineBar& bar : model.Timeline[row.Day].Bars)
				{
					if (bar.Cost > 0.0)
					{
						costs.push_back(CostPerHour(bar));
					}
				}
			}

			std::array<double, 3> cuts{};
			if (costs.empty())
			{
				return cuts;
			}

			std::ranges::sort(costs);
			for (std::size_t quarter = 0; quarter < cuts.size(); ++quarter)
			{
				cuts[quarter] = costs[(costs.size() - 1) * (quarter + 1) / (cuts.size() + 1)];
			}

			return cuts;
		}

		float BarAlpha(const TimelineBar& bar, const std::array<double, 3>& cuts)
		{
			if (bar.Cost <= 0.0)
			{
				return kBarAlphas.front();
			}

			const double cost = CostPerHour(bar);
			const auto level = static_cast<std::size_t>(std::ranges::count_if(cuts, [cost](double cut) { return cost > cut; }));
			return kBarAlphas[level];
		}

		void DrawHours(DashboardCanvas& canvas, const TimelineGrid& grid)
		{
			const float size = canvas.Units(kHourLabelSize);
			const float top = grid.FirstRowTop - canvas.Units(kHourRow);
			const ImU32 faint = Faint(canvas.Colours);

			for (int hour = 0; hour <= kHoursPerDay; hour += kHourLabelEvery)
			{
				const std::string label = std::to_string(hour);
				DrawTextAt(canvas.Drawing, ImVec2(grid.HourX(hour) - TextWidthAt(label, size) * kHalf, top), size, faint, label);
			}
		}

		void DrawBars(DashboardCanvas& canvas, const TimelineDay& day, const TimelineGrid& grid, const ImRect& track,
					  const std::array<double, 3>& cuts)
		{
			const DashboardModel& model = canvas.Model;
			const float lane = canvas.Units(kLane);
			const float laneGap = canvas.Units(kLaneGap);
			const float rounding = std::min(canvas.Units(kBarRounding), lane * kHalf);

			for (const TimelineBar& bar : day.Bars)
			{
				const float top = track.Min.y + static_cast<float>(bar.Lane) * (lane + laneGap);
				const float left = grid.HourX(bar.FirstHour);
				const float right = std::max(left + 1.0f, grid.HourX(bar.EndHour) - canvas.Units(kBarGap));
				const ImRect rect(ImVec2(left, top), ImVec2(right, top + lane));
				const ImU32 colour = canvas.Colours.Families[static_cast<std::size_t>(MainFamily(model.Sessions[bar.Session]))];

				canvas.Drawing.AddRectFilled(rect.Min, rect.Max,
											 canvas.IsHot(rect) ? Brighter(colour, canvas.Colours)
																: Core::WithAlphaScaled(colour, BarAlpha(bar, cuts)),
											 rounding);
				canvas.Add({.Rect = rect,
							.Tip =
								[&model, &calendar = canvas.Calendar, bar, date = day.Day] {
									TipCard card = SessionTip(model, bar.Session, calendar);
									card.Banner = BarBanner(date, bar);
									return card;
								},
							.OpenSession = bar.Session});
			}
		}

		void DrawLabel(DashboardCanvas& canvas, const TimelineDay& day, const TimelineGrid& grid, const TimelineRow& row)
		{
			const float small = canvas.Units(kSmallSize);
			const bool today = day.Day == canvas.Model.Today;
			const ImU32 colour = day.Gone ? Faint(canvas.Colours) : today ? canvas.Colours.Ink : canvas.Colours.Dim;
			DrawTextAt(canvas.Drawing, ImVec2(grid.Left, row.Top + (row.Height - small) * kHalf), small, colour,
					   WeekdayDate(day.Day), today ? TextFace::Strong : TextFace::Regular);
		}

		void DrawDay(DashboardCanvas& canvas, const TimelineDay& day, const TimelineGrid& grid, const TimelineRow& row,
					 const std::array<double, 3>& cuts)
		{
			const DashboardModel& model = canvas.Model;
			const ImRect whole(ImVec2(grid.Left, row.Top), ImVec2(grid.Right, row.Top + row.Height));
			const ImRect track = TimelineTrack(grid, row, day.Lanes);
			const float small = canvas.Units(kSmallSize);

			DrawLabel(canvas, day, grid, row);
			canvas.Drawing.AddRectFilled(track.Min, track.Max, canvas.Colours.Heat[0], canvas.Units(kBarRounding));

			for (int hour = kQuarterDay; hour < kHoursPerDay; hour += kQuarterDay)
			{
				const float x = std::floor(grid.HourX(hour));
				canvas.Drawing.AddLine(ImVec2(x, track.Min.y), ImVec2(x, track.Max.y), canvas.Colours.Tile);
			}

			if (const std::optional<std::size_t> index = DayIndex(model, day.Day))
			{
				const DayTotals& totals = model.Days[*index];
				const std::string cost = DayCost(totals);
				const bool spent = totals.Cost > 0.0 || totals.Requests > 0;
				const ImU32 colour = !spent ? Faint(canvas.Colours) : canvas.IsHot(whole) ? canvas.Colours.Ink : canvas.Colours.Dim;
				DrawTextAt(canvas.Drawing, ImVec2(grid.Right - TextWidthAt(cost, small), row.Top + (row.Height - small) * kHalf),
						   small, colour, cost);
				canvas.Add({.Rect = whole, .Tip = [&model, at = *index] { return DayTip(model, at); }, .ShowDay = day.Day});
			}

			DrawBars(canvas, day, grid, track, cuts);
		}

		// WARNING: The days before the oldest on disk run on to the last row, so they are hatched as one block: a hatch
		// per row would cost tens of thousands of vertices a frame on a tall panel.
		void DrawGoneDays(DashboardCanvas& canvas, const TimelineGrid& grid, std::span<const TimelineRow> rows)
		{
			if (rows.empty())
			{
				return;
			}

			for (const TimelineRow& row : rows)
			{
				const TimelineDay& day = canvas.Model.Timeline[row.Day];
				DrawLabel(canvas, day, grid, row);
				canvas.Add({.Rect = ImRect(ImVec2(grid.Left, row.Top), ImVec2(grid.Right, row.Top + row.Height)),
							.Tip = [date = day.Day] { return GoneDayTip(date); }});
			}

			const ImRect first = TimelineTrack(grid, rows.front(), 0);
			const ImRect last = TimelineTrack(grid, rows.back(), 0);
			DrawHatch(canvas.Drawing, ImRect(first.Min, last.Max), canvas.Colours.Hatch, canvas.Units(kHatchSpacing));
		}
	}

	float TimelineGrid::HourX(int hour) const
	{
		return TrackLeft + (TrackRight - TrackLeft) * static_cast<float>(hour) / static_cast<float>(kHoursPerDay);
	}

	float TimelineHeight(float /*width*/)
	{
		const float unit = ImGui::GetFontSize();
		return TileHeaderHeight(unit) + (kHourRow + static_cast<float>(kTimelineLeastDays) * kLeastRow) * unit +
			   TileBottomPadding(unit);
	}

	TimelineGrid TimelineGridOf(const ImRect& tile, const DashboardModel& model)
	{
		const float unit = ImGui::GetFontSize();
		const float small = kSmallSize * unit;
		const ImRect content = Content(tile, unit);
		TimelineGrid grid;
		grid.Left = content.Min.x;
		grid.Right = content.Max.x;
		grid.TrackLeft = content.Min.x + TextWidthAt(kWidestDay, small) + kColumnGap * unit;
		grid.TrackRight = std::max(grid.TrackLeft, content.Max.x - TextWidthAt(kWidestCost, small) - kColumnGap * unit);
		grid.FirstRowTop = content.Min.y + kHourRow * unit;

		// WARNING: Half a pixel of slack, since the least height is a sum of rows that float rounding can leave a hair
		// short.
		const float bottom = content.Max.y + kHalf;
		float top = grid.FirstRowTop;

		for (std::size_t day = 0; day < model.Timeline.size(); ++day)
		{
			const float height = RowHeight(model.Timeline[day].Lanes, unit);
			if (top + height > bottom)
			{
				break;
			}

			grid.Rows.push_back({.Day = day, .Top = top, .Height = height});
			top += height;
		}

		// WARNING: The next day may be a busy one, too tall for the room left, so that room is shared among the rows
		// shown rather than left as a band at the bottom.
		if (!grid.Rows.empty())
		{
			const float extra = std::max(0.0f, content.Max.y - top) / static_cast<float>(grid.Rows.size());

			for (std::size_t row = 0; row < grid.Rows.size(); ++row)
			{
				grid.Rows[row].Top += static_cast<float>(row) * extra;
				grid.Rows[row].Height += extra;
			}
		}

		return grid;
	}

	ImRect TimelineTrack(const TimelineGrid& grid, const TimelineRow& row, std::size_t lanes)
	{
		const float height = LanesHeight(lanes, ImGui::GetFontSize());
		const float top = row.Top + (row.Height - height) * kHalf;
		return ImRect(ImVec2(grid.TrackLeft, top), ImVec2(grid.TrackRight, top + height));
	}

	void DrawTimeline(DashboardCanvas& canvas, const ImRect& tile)
	{
		DrawTileFrame(canvas, tile, "Day by day", "sessions by local hour");
		const DashboardModel& model = canvas.Model;
		const TimelineGrid grid = TimelineGridOf(tile, model);
		const auto firstGone =
			std::ranges::find_if(grid.Rows, [&model](const TimelineRow& row) { return model.Timeline[row.Day].Gone; });
		const std::span<const TimelineRow> onDisk(grid.Rows.begin(), firstGone);
		const std::array<double, 3> cuts = QuarterCuts(model, onDisk);

		DrawHours(canvas, grid);

		for (const TimelineRow& row : onDisk)
		{
			const TimelineDay& day = model.Timeline[row.Day];

			if (std::chrono::weekday{std::chrono::sys_days{day.Day}} == std::chrono::Monday && row.Day + 1 < grid.Rows.size())
			{
				const float y = std::floor(row.Top + row.Height);
				canvas.Drawing.AddLine(ImVec2(grid.Left, y), ImVec2(grid.Right, y), canvas.Colours.Frame);
			}

			DrawDay(canvas, day, grid, row, cuts);
		}

		DrawGoneDays(canvas, grid, std::span<const TimelineRow>(firstGone, grid.Rows.end()));
	}
}
