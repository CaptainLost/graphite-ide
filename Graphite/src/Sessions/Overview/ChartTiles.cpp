#include "Sessions/Overview/ChartTiles.h"

#include "Sessions/Overview/DashboardFigures.h"
#include "Sessions/Overview/TipText.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "UI/TypographicMarks.h"

#include "Core/ImGui/ImGuiColors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace Graphite {

	namespace {
		using Date = std::chrono::year_month_day;

		using DashboardDrawing::kHalf;
		constexpr std::size_t kHoursPerDay = 24;

		// WARNING: Lengths are in font sizes, from the mockup; each is multiplied by the unit before it is drawn.
		constexpr float kNumberTopPadding = 0.5f;
		constexpr float kNumberBottomPadding = 0.35f;
		constexpr float kCaptionToValue = 0.15f;
		constexpr float kValueLine = 1.6f;
		constexpr float kDimLine = 1.0f;
		constexpr float kLineToSpark = 0.15f;
		constexpr float kSparkHeight = 1.5f;
		constexpr std::size_t kThisWeekSpan = 14;
		constexpr float kTrendTriangle = 0.45f;
		constexpr float kTrendGap = 0.25f;
		constexpr float kMarkGap = 0.2f;

		constexpr float kSparkLineWidth = 0.08f;
		constexpr float kSparkFillAlpha = 0.16f;
		constexpr float kSparkFloor = 0.05f;
		constexpr float kSparkHeadroom = 0.15f;
		constexpr float kMarkerAlpha = 0.7f;
		using DashboardDrawing::kNothing;

		constexpr float kCellPitch = 1.23f;
		constexpr float kCell = 1.0f;
		constexpr float kCellRounding = 0.12f;
		constexpr float kDayLabelGap = 0.33f;
		constexpr float kSideGap = 0.92f;
		constexpr float kSmallestBesidePitch = 1.0f;
		constexpr std::string_view kWidestTotal = "$000.00";
		constexpr std::string_view kWidestActive = "000 active days";
		constexpr std::string_view kWidestStreak = "000-day streak";
		constexpr float kTotalSize = 1.15f;
		constexpr float kSideLine = 1.15f;
		constexpr float kLegendGap = 0.3f;
		constexpr float kLegendSwatch = 0.7f;
		constexpr float kLegendSwatchGap = 0.22f;
		constexpr float kBelowGap = 0.5f;
		constexpr float kBelowRunGap = 0.6f;
		constexpr float kHintGap = 0.35f;
		constexpr float kHatchesPerCell = 3.0f;
		constexpr std::string_view kHint =
			"Hatched days are older than the oldest transcript. Claude Code keeps 30 days; `cleanupPeriodDays` keeps more.";
		constexpr std::array<std::string_view, 12> kMonths = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
															  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
		constexpr unsigned kFirstWeekOfMonth = 7;

		constexpr float kColumnsHeight = 8.0f;
		constexpr float kColumnGap = 0.17f;
		constexpr float kColumnGapShare = 0.25f;
		constexpr float kColumnRounding = 0.15f;
		constexpr float kAxisGap = 0.15f;
		constexpr float kKeysGap = 0.2f;
		constexpr float kKeyGap = 0.8f;
		constexpr float kDash = 0.35f;
		constexpr float kDashGap = 0.25f;
		constexpr float kAverageLabelLift = 1.05f;
		constexpr float kAverageLabelPadding = 0.25f;

		constexpr float kPunchLabelWidth = 2.0f;
		constexpr float kHourRow = 0.9f;
		constexpr float kPunchRow = 1.17f;
		constexpr float kHourLabelSize = 0.7f;
		constexpr float kSmallestDot = 0.25f;
		constexpr float kDotGrowth = 0.75f;
		constexpr std::size_t kHourLabelEvery = 3;

		struct NumberFace
		{
			std::string Caption;
			std::string Value;
			std::string Mark; // drawn dim after the value
			std::string Line;
			std::optional<WeekChange> Change;
			std::vector<double> Series;
			std::size_t FirstDay = 0;
			ImU32 Colour = 0;
		};

		template <typename Pick>
		std::vector<double> PerDay(const DashboardModel& model, Pick pick)
		{
			std::vector<double> values;
			values.reserve(model.Days.size());

			for (const DayTotals& day : model.Days)
			{
				values.push_back(static_cast<double>(pick(day)));
			}

			return values;
		}

		std::vector<double> CostPerDay(const DashboardModel& model)
		{
			return PerDay(model, [](const DayTotals& day) { return day.Cost; });
		}

		ImU32 FamilyColour(const DashboardColours& colours, ModelFamily family)
		{
			return colours.Families[static_cast<std::size_t>(family)];
		}

		void ThisWeekFace(const DashboardModel& model, NumberFace& face)
		{
			double now = 0.0;

			for (const WeekDay& day : ThisWeek(model))
			{
				now += day.Cost;
			}

			face.Value = CompactMoney(now);
			face.Change = ThisWeekChange(model);
			face.Line = face.Change ? " vs last week" : "nothing the same days last week";

			std::vector<double> costs = CostPerDay(model);
			face.FirstDay = costs.size() - std::min(costs.size(), kThisWeekSpan);
			face.Series.assign(costs.begin() + static_cast<std::ptrdiff_t>(face.FirstDay), costs.end());
		}

		void BusiestFace(const DashboardModel& model, NumberFace& face)
		{
			face.Series.assign(model.Days.size(), 0.0);

			if (const std::optional<std::size_t> busiest = BusiestDay(model))
			{
				const DayTotals& day = model.Days[*busiest];
				face.Value = CompactMoney(day.Cost);
				face.Line = WeekdayDate(day.Day);
				face.Series[*busiest] = day.Cost;
				return;
			}

			face.Value = std::string{kEnDash};
			face.Line = "no cost yet";
		}

		NumberFace NumberFaceOf(const DashboardModel& model, NumberTile tile, const DashboardColours& colours)
		{
			const ImU32 activity = colours.Heat[kHeatLevels - 2];
			NumberFace face;

			switch (tile)
			{
			case NumberTile::Spent:
				face = {.Caption = "Spent",
						.Value = CompactMoney(model.Cost),
						.Mark = model.UnpricedRequests > 0 ? std::string{kUnknownAmount} : std::string{},
						.Line = TipCounted(model.Days.size(), "day on disk", "days on disk"),
						.Series = CostPerDay(model),
						.Colour = colours.Accent};
				break;
			case NumberTile::ThisWeek:
				face = {.Caption = "This week", .Colour = colours.Accent};
				ThisWeekFace(model, face);
				break;
			case NumberTile::PerActiveDay:
			{
				const ActiveDayStats stats = ActiveDays(model);
				face = {.Caption = "Per active day",
						.Value = CompactMoney(stats.Average),
						.Line = std::format("{} of {} days", stats.Active, model.Days.size()),
						.Series = CostPerDay(model),
						.Colour = colours.Accent};
				break;
			}
			case NumberTile::Sessions:
				face = {.Caption = "Sessions",
						.Value = TipCount(model.Sessions.size()),
						.Line = TipCounted(model.Requests, "request", "requests"),
						.Series = PerDay(model, [](const DayTotals& day) { return day.SessionsStarted; }),
						.Colour = activity};
				break;
			case NumberTile::Subagents:
				face = {.Caption = "Subagents",
						.Value = TipShare(model.SubagentCost, model.Cost),
						.Line = CompactMoney(model.SubagentCost) + " of the cost",
						.Series = PerDay(model,
										 [](const DayTotals& day) { return day.Cost > 0.0 ? day.SubagentCost / day.Cost : 0.0; }),
						.Colour = FamilyColour(colours, ModelFamily::Sonnet)};
				break;
			case NumberTile::CacheSaved:
			{
				const TokenTotals& tokens = model.Tokens;
				const auto input = static_cast<double>(tokens.CacheRead + tokens.CacheWrite + tokens.Input);
				face = {.Caption = "Cache saved",
						.Value = CompactMoney(model.Saved),
						.Line = TipShare(static_cast<double>(tokens.CacheRead), input) + " of input from cache",
						.Series = PerDay(model, [](const DayTotals& day) { return std::max(0.0, day.Saved); }),
						.Colour = activity};
				break;
			}
			case NumberTile::Output:
				face = {.Caption = "Output",
						.Value = FormatTokens(model.Tokens.Output),
						.Line = "tokens written",
						.Series = PerDay(model, [](const DayTotals& day) { return day.Output; }),
						.Colour = activity};
				break;
			case NumberTile::BusiestDay:
				face = {.Caption = "Busiest day", .Colour = colours.Accent};
				BusiestFace(model, face);
				break;
			}

			return face;
		}

		void DrawTrend(ImDrawList& drawing, float left, float top, float size, bool rising, ImU32 colour)
		{
			const float bottom = top + size;
			const float middle = left + size * kHalf;

			if (rising)
			{
				drawing.AddTriangleFilled(ImVec2(left, bottom), ImVec2(left + size, bottom), ImVec2(middle, top), colour);
				return;
			}

			drawing.AddTriangleFilled(ImVec2(left, top), ImVec2(middle, bottom), ImVec2(left + size, top), colour);
		}

		std::optional<std::size_t> HoveredValue(const DashboardCanvas& canvas, const ImRect& box, std::size_t count)
		{
			if (count == 0 || !canvas.IsHot(box) || box.GetWidth() <= 0.0f)
			{
				return std::nullopt;
			}

			if (count == 1)
			{
				return 0;
			}

			const float along = (ImGui::GetIO().MousePos.x - box.Min.x) / box.GetWidth();
			const auto index = std::lround(along * static_cast<float>(count - 1));
			return static_cast<std::size_t>(std::clamp<long>(index, 0, static_cast<long>(count - 1)));
		}

		float SparkX(const ImRect& box, std::size_t index, std::size_t count)
		{
			if (count < 2)
			{
				return box.Min.x + box.GetWidth() * kHalf;
			}

			return box.Min.x + box.GetWidth() * static_cast<float>(index) / static_cast<float>(count - 1);
		}

		struct MapSide
		{
			std::string Total;
			std::string Active;
			std::string Streak;
		};

		MapSide MapSideOf(const DashboardModel& model)
		{
			return {.Total = CompactMoney(model.Cost),
					.Active = TipCounted(ActiveDays(model).Active, "active day", "active days"),
					.Streak = std::format("{}-day streak", Streak(model))};
		}

		float LegendWidth(float unit)
		{
			const float size = kSmallSize * unit;
			const float gap = kLegendSwatchGap * unit;
			return TextWidthAt("Less", size) + TextWidthAt("More", size) +
				   static_cast<float>(kHeatLevels) * (kLegendSwatch * unit + gap) + gap;
		}

		// WARNING: Measured on the widest text each line takes, never on the figures shown, so a figure that grows
		// cannot move the side under the grid and reflow the row.
		float SideWidth(float unit)
		{
			const float small = kSmallSize * unit;
			return std::max({TextWidthAt(kWidestTotal, kTotalSize * unit, TextFace::Strong), TextWidthAt(kWidestActive, small),
							 TextWidthAt(kWidestStreak, small), LegendWidth(unit)});
		}

		struct MapMetrics
		{
			float LabelWidth = 0.0f;
			float Pitch = 0.0f;
			float Cell = 0.0f;
			bool SideBeside = false;

			float GridWidth() const { return LabelWidth + static_cast<float>(kHeatWeeks) * Pitch - (Pitch - Cell); }

			float GridHeight() const { return static_cast<float>(kDaysPerWeek + 1) * Pitch - (Pitch - Cell); }
		};

		// WARNING: The cells shrink, down to a smallest pitch, to keep the side beside the grid; past that the side goes
		// under the grid and the cells take their full size again.
		MapMetrics MapMetricsFor(float contentWidth, float unit)
		{
			MapMetrics metrics;
			metrics.LabelWidth = TextWidthAt(WeekdayAbbreviation(0), kSmallSize * unit) + kDayLabelGap * unit;

			const float pitchesAcross = static_cast<float>(kHeatWeeks - 1) + kCell / kCellPitch;
			const float gridRoom = std::max(0.0f, contentWidth - metrics.LabelWidth);
			const float besidePitch = (gridRoom - kSideGap * unit - SideWidth(unit)) / pitchesAcross;
			metrics.SideBeside = besidePitch >= kSmallestBesidePitch * unit;
			metrics.Pitch = std::min(kCellPitch * unit, metrics.SideBeside ? besidePitch : gridRoom / pitchesAcross);
			metrics.Cell = metrics.Pitch * kCell / kCellPitch;
			return metrics;
		}

		float BelowHeight(float unit)
		{
			return (kBelowGap + kTotalSize * kLinePitch + kSideLine) * unit;
		}

		float ContentWidthOf(float tileWidth, float unit)
		{
			return std::max(0.0f, tileWidth - 2.0f * TileSidePadding(unit));
		}

		void DrawLegend(DashboardCanvas& canvas, ImVec2 at)
		{
			const float size = canvas.Units(kSmallSize);
			const float gap = canvas.Units(kLegendSwatchGap);
			const float swatch = canvas.Units(kLegendSwatch);
			const ImU32 faint = Faint(canvas.Colours);

			DrawTextAt(canvas.Drawing, at, size, faint, "Less");
			float x = at.x + TextWidthAt("Less", size) + gap;

			for (const ImU32 level : canvas.Colours.Heat)
			{
				const ImVec2 min(x, at.y + (size - swatch) * kHalf);
				canvas.Drawing.AddRectFilled(min, ImVec2(min.x + swatch, min.y + swatch), level, canvas.Units(kCellRounding));
				x += swatch + gap;
			}

			DrawTextAt(canvas.Drawing, ImVec2(x, at.y), size, faint, "More");
		}

		void DrawSideBeside(DashboardCanvas& canvas, const MapSide& side, ImVec2 at, float room)
		{
			const float small = canvas.Units(kSmallSize);
			const float total = canvas.Units(kTotalSize);
			DrawTextAt(canvas.Drawing, at, total, canvas.Colours.Ink, FitAt(side.Total, room, total, TextFace::Strong),
					   TextFace::Strong);
			float y = at.y + total * kLinePitch;
			DrawTextAt(canvas.Drawing, ImVec2(at.x, y), small, canvas.Colours.Dim, FitAt(side.Active, room, small));
			y += canvas.Units(kSideLine);
			DrawTextAt(canvas.Drawing, ImVec2(at.x, y), small, canvas.Colours.Dim, FitAt(side.Streak, room, small));
			y += canvas.Units(kSideLine + kLegendGap);
			DrawLegend(canvas, ImVec2(at.x, y));
		}

		void DrawSideBelow(DashboardCanvas& canvas, const MapSide& side, ImVec2 at, float room)
		{
			const float small = canvas.Units(kSmallSize);
			const float total = canvas.Units(kTotalSize);
			DrawTextAt(canvas.Drawing, at, total, canvas.Colours.Ink, side.Total, TextFace::Strong);

			const float runLeft = at.x + TextWidthAt(side.Total, total, TextFace::Strong) + canvas.Units(kBelowRunGap);
			const std::string run = side.Active + std::string{kMiddleDotSeparator} + side.Streak;
			DrawTextAt(canvas.Drawing, ImVec2(runLeft, at.y + (total - small) * kHalf), small, canvas.Colours.Dim,
					   FitAt(run, std::max(0.0f, at.x + room - runLeft), small));

			DrawLegend(canvas, ImVec2(at.x, at.y + total * kLinePitch));
		}

		// WARNING: Each line is cut to the cell by hand; a clip rectangle per cell would split the draw list 76 times.
		void DrawHatch(ImDrawList& drawing, const ImRect& cell, ImU32 colour)
		{
			const float side = cell.GetWidth();
			const float spacing = side / kHatchesPerCell;

			if (spacing <= 0.0f)
			{
				return;
			}

			for (float step = spacing * kHalf; step < 2.0f * side; step += spacing)
			{
				const bool upper = step <= side;
				const ImVec2 from = upper ? ImVec2(cell.Min.x + step, cell.Min.y) : ImVec2(cell.Max.x, cell.Min.y + step - side);
				const ImVec2 to = upper ? ImVec2(cell.Min.x, cell.Min.y + step) : ImVec2(cell.Min.x + step - side, cell.Max.y);
				drawing.AddLine(from, to, colour);
			}
		}

		void DrawMapCell(DashboardCanvas& canvas, const MapDay& day, const ImRect& cell)
		{
			const DashboardModel& model = canvas.Model;

			if (day.Kind == MapDayKind::Gone)
			{
				DrawHatch(canvas.Drawing, cell, canvas.Colours.Hatch);
				canvas.Add({.Rect = cell, .Tip = [date = day.Date] { return GoneDayTip(date); }});
				return;
			}

			const float rounding = canvas.Units(kCellRounding);
			canvas.Drawing.AddRectFilled(cell.Min, cell.Max, canvas.Colours.Heat[static_cast<std::size_t>(day.Level)], rounding);

			if (canvas.IsHot(cell))
			{
				canvas.Drawing.AddRect(cell.Min, cell.Max, canvas.Colours.Accent, rounding);
			}

			if (day.Index)
			{
				canvas.Add({.Rect = cell, .Tip = [&model, index = *day.Index] { return DayTip(model, index); }, .ShowDay = day.Date});
			}
		}

		void DrawMapLabels(DashboardCanvas& canvas, const MapDays& days, const MapGrid& grid, const ImRect& content)
		{
			const float small = canvas.Units(kSmallSize);
			const ImU32 dim = canvas.Colours.Dim;

			for (int week = 0; week < kHeatWeeks; ++week)
			{
				const Date monday = days.Days[static_cast<std::size_t>(week * kDaysPerWeek)].Date;

				if (static_cast<unsigned>(monday.day()) <= kFirstWeekOfMonth)
				{
					const float x = grid.FirstCell.x + static_cast<float>(week) * grid.Pitch;
					DrawTextAt(canvas.Drawing, ImVec2(x, content.Min.y + (grid.Pitch - small) * kHalf), small, dim,
							   kMonths[static_cast<unsigned>(monday.month()) - 1]);
				}
			}

			for (int weekday = 0; weekday < kDaysPerWeek; weekday += 2)
			{
				const float y = grid.FirstCell.y + static_cast<float>(weekday) * grid.Pitch + (grid.Cell - small) * kHalf;
				DrawTextAt(canvas.Drawing, ImVec2(content.Min.x, y), small, dim, WeekdayAbbreviation(weekday));
			}
		}

		void DrawDashedLine(ImDrawList& drawing, float left, float right, float y, float dash, float gap, ImU32 colour)
		{
			for (float x = left; x < right; x += dash + gap)
			{
				drawing.AddLine(ImVec2(x, y), ImVec2(std::min(x + dash, right), y), colour);
			}
		}

		void DrawDailyColumns(DashboardCanvas& canvas, const ImRect& area, double highest)
		{
			const DashboardModel& model = canvas.Model;
			const std::size_t count = model.Days.size();
			const float slot = area.GetWidth() / static_cast<float>(count);
			const float gap = std::min(canvas.Units(kColumnGap), slot * kColumnGapShare);
			const auto height = static_cast<double>(area.GetHeight());

			for (std::size_t index = 0; index < count; ++index)
			{
				const DayTotals& day = model.Days[index];
				const float left = area.Min.x + static_cast<float>(index) * slot;
				const ImRect column(ImVec2(left, area.Min.y), ImVec2(left + slot, area.Max.y));
				const bool hot = canvas.IsHot(column);
				float bottom = area.Max.y;
				std::size_t topFamily = kModelFamilyCount;

				for (std::size_t family = 0; family < kModelFamilyCount; ++family)
				{
					topFamily = day.CostByFamily[family] > 0.0 ? family : topFamily;
				}

				// WARNING: Only the column's top piece is rounded, and only at its top, as one bar's cap.
				for (std::size_t family = 0; family < kModelFamilyCount; ++family)
				{
					const auto piece = static_cast<float>(day.CostByFamily[family] / highest * height);

					if (piece > 0.0f)
					{
						const ImU32 colour = canvas.Colours.Families[family];
						const bool top = family == topFamily;
						canvas.Drawing.AddRectFilled(ImVec2(left, bottom - piece), ImVec2(left + slot - gap, bottom),
													 hot ? Brighter(colour, canvas.Colours) : colour,
													 top ? canvas.Units(kColumnRounding) : 0.0f,
													 top ? ImDrawFlags_RoundCornersTop : ImDrawFlags_None);
						bottom -= piece;
					}
				}

				canvas.Add({.Rect = column, .Tip = [&model, index] { return DayTip(model, index); }, .ShowDay = day.Day});
			}
		}

		void DrawAverage(DashboardCanvas& canvas, const ImRect& area, double highest)
		{
			const double average = ActiveDays(canvas.Model).Average;

			if (average <= 0.0)
			{
				return;
			}

			const float y = area.Max.y - static_cast<float>(average / highest * static_cast<double>(area.GetHeight()));
			DrawDashedLine(canvas.Drawing, area.Min.x, area.Max.x, y, canvas.Units(kDash), canvas.Units(kDashGap),
						   canvas.Colours.Dim);

			const float small = canvas.Units(kSmallSize);
			const std::string label = "avg " + CompactMoney(average);
			const float width = TextWidthAt(label, small);
			const float padding = canvas.Units(kAverageLabelPadding);
			const ImVec2 at(area.Max.x - width, std::max(area.Min.y, y - small * kAverageLabelLift));
			canvas.Drawing.AddRectFilled(ImVec2(at.x - padding, at.y), ImVec2(area.Max.x, at.y + small), canvas.Colours.Tile);
			DrawTextAt(canvas.Drawing, at, small, canvas.Colours.Dim, label);
		}

		void DrawDailyAxis(DashboardCanvas& canvas, float left, float right, float top)
		{
			const auto& days = canvas.Model.Days;
			const float small = canvas.Units(kSmallSize);
			const ImU32 faint = Faint(canvas.Colours);

			const std::string first = ShortDate(days.front().Day);
			DrawTextAt(canvas.Drawing, ImVec2(left, top), small, faint, first);

			if (days.size() < 2)
			{
				return;
			}

			const std::string last = ShortDate(days.back().Day);
			const float lastWidth = TextWidthAt(last, small);
			const float firstWidth = TextWidthAt(first, small);
			const float room = right - left;

			if (firstWidth + lastWidth > room)
			{
				return;
			}

			DrawTextAt(canvas.Drawing, ImVec2(right - lastWidth, top), small, faint, last);

			const std::string middle = ShortDate(days[days.size() / 2].Day);
			const float middleWidth = TextWidthAt(middle, small);
			const float gap = canvas.Units(kKeyGap);

			if (days.size() > 2 && firstWidth + middleWidth + lastWidth + 2.0f * gap <= room)
			{
				DrawTextAt(canvas.Drawing, ImVec2(left + (room - middleWidth) * kHalf, top), small, faint, middle);
			}
		}

		void DrawFamilyKeys(DashboardCanvas& canvas, float left, float right, float top)
		{
			const float small = canvas.Units(kSmallSize);
			const float pitch = small * kLinePitch;
			float x = left;

			for (std::size_t family = 0; family < kModelFamilyCount; ++family)
			{
				if (canvas.Model.Families[family].Requests == 0 && canvas.Model.Families[family].Cost <= 0.0)
				{
					continue;
				}

				const std::string name = FamilyLabel(canvas.Model, static_cast<ModelFamily>(family));
				const float width = SwatchRoom(canvas.Unit) + TextWidthAt(name, small);

				if (x + width > right)
				{
					return;
				}

				DrawSwatch(canvas.Drawing, canvas.Unit, x, top, pitch, canvas.Colours.Families[family]);
				DrawTextAt(canvas.Drawing, ImVec2(x + SwatchRoom(canvas.Unit), top + (pitch - small) * kHalf), small,
						   canvas.Colours.Dim, name);
				x += width + canvas.Units(kKeyGap);
			}
		}

		double HighestDay(const DashboardModel& model)
		{
			double highest = 0.0;

			for (const DayTotals& day : model.Days)
			{
				highest = std::max(highest, day.Cost);
			}

			return highest;
		}

		double CostiestHour(const DashboardModel& model)
		{
			double highest = 0.0;

			for (const auto& weekday : model.Punch)
			{
				for (const HourCell& cell : weekday)
				{
					highest = std::max(highest, cell.Cost);
				}
			}

			return highest;
		}
	}

	std::optional<WeekChange> ThisWeekChange(const DashboardModel& model)
	{
		double now = 0.0;
		double before = 0.0;

		for (const WeekDay& day : ThisWeek(model))
		{
			now += day.Cost;
			before += day.WeekBefore;
		}

		if (before <= 0.0)
		{
			return std::nullopt;
		}

		const long percent = std::lround(100.0 * (now - before) / before);
		WeekChange change{.Percent = std::format("{}%", std::abs(percent))};

		if (percent != 0)
		{
			change.Rising = percent > 0;
		}

		return change;
	}

	float NumberTileHeight(float unit)
	{
		return (kNumberTopPadding + kCaptionSize + kCaptionToValue + kValueLine + kDimLine + kLineToSpark + kSparkHeight +
				kNumberBottomPadding) *
			   unit;
	}

	void DrawNumberTile(DashboardCanvas& canvas, const ImRect& tile, NumberTile which)
	{
		DrawTileBackground(canvas, tile);

		const NumberFace face = NumberFaceOf(canvas.Model, which, canvas.Colours);
		const float left = tile.Min.x + TileSidePadding(canvas.Unit);
		const float room = ContentWidthOf(tile.GetWidth(), canvas.Unit);
		const float caption = canvas.Units(kCaptionSize);
		const float value = canvas.Units(kValueSize);
		const float small = canvas.Units(kSmallSize);
		float y = tile.Min.y + canvas.Units(kNumberTopPadding);

		DrawTextAt(canvas.Drawing, ImVec2(left, y), caption, canvas.Colours.Dim, FitAt(UpperCase(face.Caption), room, caption));
		y += caption + canvas.Units(kCaptionToValue);

		const float markRoom = face.Mark.empty() ? 0.0f : TextWidthAt(face.Mark, value) + canvas.Units(kMarkGap);
		const std::string shownValue = FitAt(face.Value, std::max(0.0f, room - markRoom), value, TextFace::Strong);
		DrawTextAt(canvas.Drawing, ImVec2(left, y), value, canvas.Colours.Ink, shownValue, TextFace::Strong);

		if (!face.Mark.empty() && shownValue == face.Value)
		{
			DrawTextAt(canvas.Drawing, ImVec2(left + TextWidthAt(shownValue, value, TextFace::Strong) + canvas.Units(kMarkGap), y),
					   value, canvas.Colours.Dim, face.Mark);
		}

		y += canvas.Units(kValueLine);

		float x = left;
		if (face.Change && face.Change->Rising)
		{
			const bool rising = *face.Change->Rising;
			const ImU32 tone = rising ? canvas.Colours.Bad : canvas.Colours.Good;
			const float triangle = canvas.Units(kTrendTriangle);
			DrawTrend(canvas.Drawing, x, y + (small - triangle) * kHalf, triangle, rising, tone);
			x += triangle + canvas.Units(kTrendGap);
			DrawTextAt(canvas.Drawing, ImVec2(x, y), small, tone, face.Change->Percent);
			x += TextWidthAt(face.Change->Percent, small);
		}
		else if (face.Change)
		{
			DrawTextAt(canvas.Drawing, ImVec2(x, y), small, Faint(canvas.Colours), face.Change->Percent);
			x += TextWidthAt(face.Change->Percent, small);
		}

		DrawTextAt(canvas.Drawing, ImVec2(x, y), small, Faint(canvas.Colours),
				   FitAt(face.Line, std::max(0.0f, left + room - x), small));
		y += canvas.Units(kDimLine + kLineToSpark);

		const ImRect spark(ImVec2(left, y), ImVec2(left + room, tile.Max.y - canvas.Units(kNumberBottomPadding)));
		const std::optional<std::size_t> hovered = HoveredValue(canvas, spark, face.Series.size());
		DrawSparkline(canvas.Drawing, spark, face.Series, face.Colour, hovered,
					  Core::WithAlphaScaled(canvas.Colours.Ink, kMarkerAlpha));

		const std::optional<std::size_t> hoveredDay =
			hovered ? std::optional<std::size_t>{face.FirstDay + *hovered} : std::nullopt;
		canvas.Add({.Rect = tile, .Tip = [&model = canvas.Model, which, hoveredDay] { return NumberTip(model, which, hoveredDay); }});
	}

	void DrawSparkline(ImDrawList& drawing, const ImRect& box, std::span<const double> values, ImU32 colour,
					   std::optional<std::size_t> marker, ImU32 markerColour)
	{
		if (values.empty() || box.GetWidth() <= 0.0f || box.GetHeight() <= 0.0f)
		{
			return;
		}

		const double highest = std::max(*std::ranges::max_element(values), kNothing);
		const float floor = box.Max.y - box.GetHeight() * kSparkFloor;
		const float rise = box.GetHeight() * (1.0f - kSparkFloor - kSparkHeadroom);

		std::vector<ImVec2> points;
		points.reserve(std::max<std::size_t>(values.size(), 2));

		for (std::size_t index = 0; index < values.size(); ++index)
		{
			const auto share = static_cast<float>(std::max(0.0, values[index]) / highest);
			points.emplace_back(SparkX(box, index, values.size()), floor - share * rise);
		}

		if (points.size() == 1)
		{
			points = {ImVec2(box.Min.x, points.front().y), ImVec2(box.Max.x, points.front().y)};
		}

		// WARNING: The area is drawn as one quad per step; with anti-aliasing each quad's fringe would show as a seam.
		const ImDrawListFlags flags = drawing.Flags;
		drawing.Flags &= ~ImDrawListFlags_AntiAliasedFill;
		const ImU32 fill = Core::WithAlphaScaled(colour, kSparkFillAlpha);

		for (std::size_t index = 0; index + 1 < points.size(); ++index)
		{
			const ImVec2 from = points[index];
			const ImVec2 to = points[index + 1];
			drawing.AddQuadFilled(from, to, ImVec2(to.x, box.Max.y), ImVec2(from.x, box.Max.y), fill);
		}

		drawing.Flags = flags;

		const float unit = ImGui::GetFontSize();
		drawing.AddPolyline(points.data(), static_cast<int>(points.size()), colour, ImDrawFlags_None,
							std::max(1.0f, kSparkLineWidth * unit));

		if (marker && *marker < values.size())
		{
			const float x = SparkX(box, *marker, values.size());
			drawing.AddLine(ImVec2(x, box.Min.y), ImVec2(x, box.Max.y), markerColour);
		}
	}

	MapGrid MapGridOf(const ImRect& tile)
	{
		const float unit = ImGui::GetFontSize();
		const MapMetrics metrics = MapMetricsFor(ContentWidthOf(tile.GetWidth(), unit), unit);
		const float left = tile.Min.x + TileSidePadding(unit);
		const float top = tile.Min.y + TileHeaderHeight(unit);
		return {.FirstCell = ImVec2(left + metrics.LabelWidth, top + metrics.Pitch), .Pitch = metrics.Pitch, .Cell = metrics.Cell};
	}

	bool MapSideStandsBeside(float width)
	{
		const float unit = ImGui::GetFontSize();
		return MapMetricsFor(ContentWidthOf(width, unit), unit).SideBeside;
	}

	float MapHeight(float width)
	{
		const float unit = ImGui::GetFontSize();
		const float content = ContentWidthOf(width, unit);
		const MapMetrics metrics = MapMetricsFor(content, unit);

		float height = TileHeaderHeight(unit) + metrics.GridHeight();

		if (!metrics.SideBeside)
		{
			height += BelowHeight(unit);
		}

		return height + kHintGap * unit + WrappedRunsHeightAt(kHint, kSmallSize * unit, content) + TileBottomPadding(unit);
	}

	void DrawMap(DashboardCanvas& canvas, const ImRect& tile)
	{
		const ImRect content = DrawTileFrame(canvas, tile, "Last 13 weeks", "cost per day");
		const MapGrid grid = MapGridOf(tile);
		const MapMetrics metrics = MapMetricsFor(content.GetWidth(), canvas.Unit);
		const MapDays days = MapDaysOf(canvas.Model);

		DrawMapLabels(canvas, days, grid, content);

		for (std::size_t slot = 0; slot < kMapDayCount; ++slot)
		{
			const MapDay& day = days.Days[slot];

			if (day.Kind == MapDayKind::Future)
			{
				continue;
			}

			const auto week = static_cast<float>(slot / kDaysPerWeek);
			const auto weekday = static_cast<float>(slot % kDaysPerWeek);
			const ImVec2 min(grid.FirstCell.x + week * grid.Pitch, grid.FirstCell.y + weekday * grid.Pitch);
			DrawMapCell(canvas, day, ImRect(min, ImVec2(min.x + grid.Cell, min.y + grid.Cell)));
		}

		const MapSide side = MapSideOf(canvas.Model);
		float below = content.Min.y + metrics.GridHeight();

		if (metrics.SideBeside)
		{
			const float sideLeft = content.Min.x + metrics.GridWidth() + canvas.Units(kSideGap);
			DrawSideBeside(canvas, side, ImVec2(sideLeft, content.Min.y), content.Max.x - sideLeft);
		}
		else
		{
			DrawSideBelow(canvas, side, ImVec2(content.Min.x, below + canvas.Units(kBelowGap)), content.GetWidth());
			below += BelowHeight(canvas.Unit);
		}

		DrawWrappedRunsAt(canvas.Drawing, ImVec2(content.Min.x, below + canvas.Units(kHintGap)), canvas.Units(kSmallSize),
						  Faint(canvas.Colours), canvas.Colours.Dim, kHint, content.GetWidth());
	}

	float DailyHeight(float /*width*/)
	{
		const float unit = ImGui::GetFontSize();
		return TileHeaderHeight(unit) +
			   (kColumnsHeight + kAxisGap + kSmallSize * kLinePitch + kKeysGap + kSmallSize * kLinePitch) * unit +
			   TileBottomPadding(unit);
	}

	void DrawDaily(DashboardCanvas& canvas, const ImRect& tile)
	{
		const double highest = HighestDay(canvas.Model);
		const ImRect content =
			DrawTileFrame(canvas, tile, "Cost per day", canvas.Model.Days.empty() ? std::string{} : "max " + CompactMoney(highest));
		const float under = canvas.Units(kAxisGap + kSmallSize * kLinePitch + kKeysGap + kSmallSize * kLinePitch);
		const float columns = std::max(canvas.Units(kColumnsHeight), content.GetHeight() - under);
		const ImRect area(content.Min, ImVec2(content.Max.x, content.Min.y + columns));

		canvas.Drawing.AddLine(ImVec2(area.Min.x, area.Max.y), area.Max, canvas.Colours.Frame);

		if (canvas.Model.Days.empty())
		{
			return;
		}

		if (highest > 0.0)
		{
			DrawDailyColumns(canvas, area, highest);
			DrawAverage(canvas, area, highest);
		}

		const float axisTop = area.Max.y + canvas.Units(kAxisGap);
		DrawDailyAxis(canvas, area.Min.x, area.Max.x, axisTop);
		DrawFamilyKeys(canvas, area.Min.x, area.Max.x, axisTop + canvas.Units(kSmallSize * kLinePitch + kKeysGap));
	}

	float PunchHeight(float /*width*/)
	{
		const float unit = ImGui::GetFontSize();
		return TileHeaderHeight(unit) + (kHourRow + static_cast<float>(kDaysPerWeek) * kPunchRow) * unit +
			   TileBottomPadding(unit);
	}

	void DrawPunch(DashboardCanvas& canvas, const ImRect& tile)
	{
		const ImRect content = DrawTileFrame(canvas, tile, "When you work", "cost by weekday and hour");
		const DashboardModel& model = canvas.Model;
		const float labelWidth = canvas.Units(kPunchLabelWidth);
		const float column = std::max(0.0f, content.GetWidth() - labelWidth) / static_cast<float>(kHoursPerDay);
		const float row = canvas.Units(kPunchRow);
		const float hourSize = canvas.Units(kHourLabelSize);
		const float small = canvas.Units(kSmallSize);
		const float left = content.Min.x + labelWidth;
		const ImU32 faint = Faint(canvas.Colours);

		for (std::size_t hour = 0; hour < kHoursPerDay; hour += kHourLabelEvery)
		{
			const std::string label = std::to_string(hour);
			const float x = left + (static_cast<float>(hour) + kHalf) * column - TextWidthAt(label, hourSize) * kHalf;
			DrawTextAt(canvas.Drawing, ImVec2(x, content.Min.y), hourSize, faint, label);
		}

		const double highest = std::max(CostiestHour(model), kNothing);
		const ImU32 dot = canvas.Colours.Heat[kHeatLevels - 2];
		const float largest = std::max(1.0f, std::min(column, row) - 1.0f);

		for (int weekday = 0; weekday < kDaysPerWeek; ++weekday)
		{
			const float top = content.Min.y + canvas.Units(kHourRow) + static_cast<float>(weekday) * row;
			DrawTextAt(canvas.Drawing, ImVec2(content.Min.x, top + (row - small) * kHalf), small, canvas.Colours.Dim,
					   WeekdayAbbreviation(weekday));

			for (std::size_t hour = 0; hour < kHoursPerDay; ++hour)
			{
				const HourCell& cell = model.Punch[static_cast<std::size_t>(weekday)][hour];

				if (cell.Cost <= 0.0 && cell.Requests == 0)
				{
					continue;
				}

				const ImRect box(ImVec2(left + static_cast<float>(hour) * column, top),
								 ImVec2(left + static_cast<float>(hour + 1) * column, top + row));
				const auto growth = static_cast<float>(std::sqrt(std::max(0.0, cell.Cost) / highest));
				const float diameter = std::min(largest, canvas.Units(kSmallestDot + kDotGrowth * growth));
				canvas.Drawing.AddCircleFilled(box.GetCenter(), diameter * kHalf,
											   canvas.IsHot(box) ? Brighter(dot, canvas.Colours) : dot);
				canvas.Add({.Rect = box,
							.Tip = [&model, weekday, hour = static_cast<int>(hour)] { return PunchTip(model, weekday, hour); }});
			}
		}
	}
}
