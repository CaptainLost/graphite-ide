#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "DrawnText.h"
#include "ImGuiFrames.h"

#include "Sessions/Overview/ChartTiles.h"
#include "Sessions/Overview/DashboardColours.h"
#include "Sessions/Overview/DashboardLayout.h"
#include "Sessions/Overview/DashboardView.h"
#include "Sessions/Overview/ListTiles.h"
#include "Sessions/Overview/TipCardView.h"
#include "Theme/SessionPalette.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/FontSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <optional>
#include <vector>

using namespace Graphite;

namespace {
	constexpr std::uint64_t kOneDollarOfOpusInput = 200'000;
	constexpr float kTallWindow = 2000.0f;
	constexpr float kHalf = 0.5f;
	constexpr float kValueTop = 1.45f; // the number tile's value line, in font sizes from its top

	std::chrono::year_month_day Day(int year, unsigned month, unsigned day)
	{
		return std::chrono::year{year} / month / day;
	}

	UtcHour Hour(int year, unsigned month, unsigned day, int hour)
	{
		return std::chrono::sys_days{Day(year, month, day)} + std::chrono::hours{hour};
	}

	HourActivity OpusDollars(double dollars)
	{
		HourActivity activity;
		const UsageKey key{"claude-opus-5", false};
		activity.Usage[key] = {static_cast<std::uint64_t>(dollars * static_cast<double>(kOneDollarOfOpusInput)), 0, 0, 0, 0};
		activity.Requests = 1;
		activity.RequestsByModel[key] = 1;
		activity.Prompts = 1;
		return activity;
	}

	ImU32 Grey(int level)
	{
		return IM_COL32(level, level, level, 255);
	}

	// Every colour told apart from every other, so a vertex in one names what drew it.
	DashboardColours DistinctColours()
	{
		DashboardColours colours{};
		colours.Families = {IM_COL32(250, 1, 1, 255), IM_COL32(1, 250, 1, 255), IM_COL32(1, 1, 250, 255),
							IM_COL32(250, 250, 1, 255), IM_COL32(250, 1, 250, 255)};
		colours.Heat = {IM_COL32(40, 41, 42, 255), IM_COL32(10, 60, 20, 255), IM_COL32(10, 90, 30, 255),
						IM_COL32(20, 140, 50, 255), IM_COL32(40, 200, 70, 255)};
		colours.Hatch = IM_COL32(120, 121, 122, 90);
		colours.Bar = IM_COL32(45, 60, 40, 255);
		colours.Ink = Grey(210);
		colours.Dim = Grey(140);
		colours.Tile = Grey(30);
		colours.Frame = Grey(50);
		colours.Accent = IM_COL32(226, 176, 74, 255);
		colours.Bad = IM_COL32(215, 154, 138, 255);
		colours.Good = IM_COL32(140, 194, 101, 255);
		return colours;
	}

	// Usage from Sunday 1 Nov to today, Tuesday 10 Nov 2026: one large session and one small, all Opus.
	struct Fixture
	{
		SessionSummary Large;
		SessionSummary Small;
		DashboardModel Model;
		LocalCalendar Calendar{nullptr};

		Fixture()
		{
			Large.Id = "large";
			Large.Cwd = Core::FromUtf8("D:\\Repos\\IDE");
			Large.Title = "Build the dashboard";
			Large.StartedAt = "2026-11-01T09:00:00.000Z";
			Large.EndedAt = "2026-11-10T10:00:00.000Z";
			Large.Activity.MainHours[Hour(2026, 11, 1, 10)] = OpusDollars(2);
			Large.Activity.MainHours[Hour(2026, 11, 10, 9)] = OpusDollars(1);
			Large.Activity.Tools["Bash"] = {5, 0};

			Small.Id = "small";
			Small.Cwd = Core::FromUtf8("D:\\Repos\\Notes");
			Small.Title = "Fix the list";
			Small.StartedAt = "2026-11-05T10:00:00.000Z";
			Small.EndedAt = "2026-11-05T11:00:00.000Z";
			Small.Activity.MainHours[Hour(2026, 11, 5, 10)] = OpusDollars(0.5);

			const std::vector<const SessionSummary*> sessions{&Large, &Small};
			Model = BuildDashboard(sessions, Pricing::Defaults(), Calendar, Day(2026, 11, 10));
		}
	};

	// The dashboard alone in a window at the screen's corner, without padding, so its origin is the window's.
	struct DashboardScreen
	{
		ImGuiFrames Frames;
		Fixture Data;
		DashboardColours Colours = DistinctColours();
		float Width = 0.0f;
		ImVec2 Origin;
		DashboardGrid Grid;
		DashboardAction LastAction;
		std::optional<DashboardAction> Acted;

		explicit DashboardScreen(float widthInFontSizes)
		{
			ImGui::GetIO().DisplaySize = ImVec2(kTallWindow, kTallWindow);
			ImGuiStyle& style = ImGui::GetStyle();
			style.FontSizeBase = Core::kInterfaceFontSize;
			style.WindowPadding = ImVec2(0.0f, 0.0f);
			Width = widthInFontSizes * Core::kInterfaceFontSize;
		}

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(Width, kTallWindow));
			ImGui::Begin("Panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			Origin = ImGui::GetCursorScreenPos();
			LastAction = DrawDashboard(Data.Model, Data.Calendar, Colours);
			Grid = LayOutDashboard(Width);

			if (LastAction.ShowDay || LastAction.OpenSession)
			{
				Acted = LastAction;
			}

			ImGui::End();
		}

		std::function<void()> Drawing()
		{
			return [this] { Draw(); };
		}

		void DrawFrame() { Frames.Next(Drawing()); }

		ImRect OnScreen(const ImRect& rect) const
		{
			return ImRect(ImVec2(rect.Min.x + Origin.x, rect.Min.y + Origin.y), ImVec2(rect.Max.x + Origin.x, rect.Max.y + Origin.y));
		}

		static const ImDrawList& DashboardDrawList()
		{
			const ImGuiWindow* panel = ImGui::FindWindowByName("Panel");
			REQUIRE(panel != nullptr);
			REQUIRE(!panel->DC.ChildWindows.empty());
			return *panel->DC.ChildWindows.back()->DrawList;
		}
	};

	const ImGuiWindow* ShownTooltip()
	{
		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if ((window->Flags & ImGuiWindowFlags_Tooltip) != 0 && window->Active)
			{
				return window;
			}
		}

		return nullptr;
	}

	int VerticesInColourWithin(const ImDrawList& drawing, ImU32 colour, const ImRect& rect)
	{
		int count = 0;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col == colour && rect.Contains(vertex.pos))
			{
				++count;
			}
		}

		return count;
	}

	std::vector<ImRect> AllTiles(const DashboardGrid& grid)
	{
		std::vector<ImRect> tiles(grid.Numbers.begin(), grid.Numbers.end());
		tiles.insert(tiles.end(), grid.Areas.begin(), grid.Areas.end());
		return tiles;
	}

	bool Overlap(const ImRect& left, const ImRect& right)
	{
		return left.Min.x < right.Max.x && right.Min.x < left.Max.x && left.Min.y < right.Max.y &&
			   right.Min.y < left.Max.y;
	}

	const ImRect& AreaOf(const DashboardGrid& grid, DashboardArea area)
	{
		return grid.Areas[static_cast<std::size_t>(area)];
	}
}

TEST_CASE("Draws 13 weeks from Monday 17 Aug, hatching the days before the oldest one")
{
	DashboardScreen screen(kWideDashboardInFontSizes);
	screen.DrawFrame();

	const MapDays days = MapDaysOf(screen.Data.Model);
	CHECK(days.FirstMonday == Day(2026, 8, 17));

	const auto count = [&](MapDayKind kind) {
		return std::count_if(days.Days.begin(), days.Days.end(), [&](const MapDay& day) { return day.Kind == kind; });
	};

	CHECK(count(MapDayKind::Future) == 5);
	CHECK(count(MapDayKind::Gone) == 76);
	CHECK(count(MapDayKind::OnDisk) == 10);
	CHECK(days.Days[75].Date == Day(2026, 10, 31));
	CHECK(days.Days[75].Kind == MapDayKind::Gone);
	CHECK(days.Days[76].Date == Day(2026, 11, 1));
	CHECK(days.Days[76].Kind == MapDayKind::OnDisk);

	const ImRect map = screen.OnScreen(AreaOf(screen.Grid, DashboardArea::Map));
	const ImDrawList& drawing = DashboardScreen::DashboardDrawList();
	CHECK(VerticesInColourWithin(drawing, screen.Colours.Hatch, map) > 0);
	CHECK(VerticesInColourWithin(drawing, screen.Colours.Heat[0], map) > 0);
}

TEST_CASE("A click on a heat cell asks to show that day")
{
	DashboardScreen screen(kWideDashboardInFontSizes);
	MapGrid grid;
	screen.Frames.Next([&] {
		screen.Draw();
		grid = MapGridOf(screen.OnScreen(AreaOf(screen.Grid, DashboardArea::Map)));
	});

	constexpr int kLastWeek = kHeatWeeks - 1;
	constexpr int kTuesday = 1;
	const ImVec2 cell(grid.FirstCell.x + static_cast<float>(kLastWeek) * grid.Pitch + grid.Cell * kHalf,
					  grid.FirstCell.y + static_cast<float>(kTuesday) * grid.Pitch + grid.Cell * kHalf);

	screen.Frames.Click(cell, screen.Drawing());

	REQUIRE(screen.Acted.has_value());
	REQUIRE(screen.Acted->ShowDay.has_value());
	CHECK(*screen.Acted->ShowDay == Day(2026, 11, 10));
	CHECK_FALSE(screen.Acted->OpenSession.has_value());
}

TEST_CASE("A click on an expensive session asks to open it")
{
	DashboardScreen screen(kWideDashboardInFontSizes);
	screen.DrawFrame();

	const ImRect row = ListRowRect(screen.OnScreen(AreaOf(screen.Grid, DashboardArea::Sessions)), 1);
	screen.Frames.Click(row.GetCenter(), screen.Drawing());

	REQUIRE(screen.Acted.has_value());
	REQUIRE(screen.Acted->OpenSession.has_value());
	CHECK(*screen.Acted->OpenSession == 1);
	CHECK(screen.Data.Model.Sessions[1].Summary == &screen.Data.Small);
	CHECK_FALSE(screen.Acted->ShowDay.has_value());
}

TEST_CASE("Hovering a tool row shows its tooltip")
{
	DashboardScreen screen(kWideDashboardInFontSizes);
	screen.DrawFrame();
	CHECK(ShownTooltip() == nullptr);

	const ImRect row = ListRowRect(screen.OnScreen(AreaOf(screen.Grid, DashboardArea::Tools)), 0);
	ImGui::GetIO().AddMousePosEvent(row.GetCenter().x, row.GetCenter().y);
	// A tooltip's first frame sizes it; its text is clipped until the second.
	screen.DrawFrame();

	std::vector<DrawnLine> lines;
	screen.Frames.Next([&] {
		screen.Draw();
		const ImGuiWindow* tooltip = ShownTooltip();
		REQUIRE(tooltip != nullptr);
		lines = DrawnTextLines(*tooltip->DrawList, *ImGui::GetFont()->GetFontBaked(ImGui::GetFontSize()));
	});

	REQUIRE(!lines.empty());
	CHECK(lines.front().Text.starts_with("Bash"));
}

TEST_CASE("Requests to a model without a price mark the Spent tile and keep their family on the Models tile")
{
	DashboardScreen screen(kWideDashboardInFontSizes);
	HourActivity unpriced;
	const UsageKey key{"claude-unknown-9", false};
	unpriced.Usage[key] = {1000, 0, 0, 0, 0};
	unpriced.Requests = 1;
	unpriced.RequestsByModel[key] = 1;
	screen.Data.Large.Activity.MainHours[Hour(2026, 11, 9, 9)] = unpriced;
	const std::vector<const SessionSummary*> sessions{&screen.Data.Large, &screen.Data.Small};
	screen.Data.Model = BuildDashboard(sessions, Pricing::Defaults(), screen.Data.Calendar, Day(2026, 11, 10));

	std::vector<DrawnLine> bodies;
	int dimInValueLine = 0;
	const auto read = [&] {
		screen.Draw();
		const ImDrawList& drawing = DashboardScreen::DashboardDrawList();
		bodies = DrawnTextLines(drawing, *ImGui::GetFont()->GetFontBaked(kBodySize * ImGui::GetFontSize()));
		const ImRect spent = screen.OnScreen(screen.Grid.Numbers[0]);
		const float unit = ImGui::GetFontSize();
		const ImRect valueLine(ImVec2(spent.Min.x, spent.Min.y + kValueTop * unit),
							   ImVec2(spent.Max.x, spent.Min.y + (kValueTop + kValueSize) * unit));
		dimInValueLine = VerticesInColourWithin(drawing, screen.Colours.Dim, valueLine);
	};
	screen.Frames.Next(read);
	screen.Frames.Next(read);

	CHECK(dimInValueLine > 0);

	const ImRect models = screen.OnScreen(AreaOf(screen.Grid, DashboardArea::Models));
	CHECK(std::ranges::any_of(bodies, [&](const DrawnLine& line) {
		return line.Text == "unknown-9?" && line.Top >= models.Min.y && line.Top < models.Max.y;
	}));
}

TEST_CASE("The Spent tile carries no mark when every request has a price")
{
	DashboardScreen screen(kWideDashboardInFontSizes);
	int dimInValueLine = -1;
	screen.Frames.Next([&] {
		screen.Draw();
		const ImRect spent = screen.OnScreen(screen.Grid.Numbers[0]);
		const float unit = ImGui::GetFontSize();
		const ImRect valueLine(ImVec2(spent.Min.x, spent.Min.y + kValueTop * unit),
							   ImVec2(spent.Max.x, spent.Min.y + (kValueTop + kValueSize) * unit));
		dimInValueLine = VerticesInColourWithin(DashboardScreen::DashboardDrawList(), screen.Colours.Dim, valueLine);
	});

	CHECK(dimInValueLine == 0);
}

TEST_CASE("A flat week has no triangle and a neutral nought, a rise one pointing up")
{
	SessionSummary summary;
	summary.Id = "a";
	summary.Cwd = Core::FromUtf8("D:/Repos/IDE");
	summary.Activity.MainHours[Hour(2026, 11, 2, 9)] = OpusDollars(1);
	summary.Activity.MainHours[Hour(2026, 11, 9, 9)] = OpusDollars(1);
	const std::vector<const SessionSummary*> sessions{&summary};
	const LocalCalendar utc{nullptr};

	const std::optional<WeekChange> flat =
		ThisWeekChange(BuildDashboard(sessions, Pricing::Defaults(), utc, Day(2026, 11, 10)));
	REQUIRE(flat.has_value());
	CHECK_FALSE(flat->Rising.has_value());
	CHECK(flat->Percent == "0%");

	summary.Activity.MainHours[Hour(2026, 11, 10, 9)] = OpusDollars(1);
	const std::optional<WeekChange> rising =
		ThisWeekChange(BuildDashboard(sessions, Pricing::Defaults(), utc, Day(2026, 11, 10)));
	REQUIRE(rising.has_value());
	CHECK(rising->Rising == std::optional<bool>{true});
	CHECK(rising->Percent == "100%");

	CHECK_FALSE(ThisWeekChange(BuildDashboard(sessions, Pricing::Defaults(), utc, Day(2026, 11, 30))).has_value());
}

TEST_CASE("When the cache cost more than it saved, the sentence still reads true")
{
	SessionSummary summary;
	summary.Id = "a";
	summary.Cwd = Core::FromUtf8("D:/Repos/IDE");
	HourActivity written;
	const UsageKey key{"claude-haiku-4-5", false};
	written.Usage[key] = {0, 0, 0, 1'000'000, 0};
	written.Requests = 1;
	written.RequestsByModel[key] = 1;
	summary.Activity.MainHours[Hour(2026, 11, 9, 9)] = written;
	const std::vector<const SessionSummary*> sessions{&summary};
	const DashboardModel model = BuildDashboard(sessions, Pricing::Defaults(), LocalCalendar{nullptr}, Day(2026, 11, 10));

	REQUIRE(model.Saved < 0.0);
	CHECK(CacheSentenceOf(model).Whole() == "These requests would have cost $1.00 without the cache, not $2.00.");
	CHECK(CacheSentenceOf(model).Without == "$1.00");

	DashboardScreen screen(kWideDashboardInFontSizes);
	CHECK(CacheSentenceOf(screen.Data.Model).Whole() == "Without the cache this would have cost $3.50, not $3.50.");
}

TEST_CASE("Tile heights follow the width alone, so a total crossing $1000 or a loss on the cache moves no tile")
{
	const auto modelCosting = [](SessionSummary& summary, double dollars, std::uint64_t cacheWrites) {
		summary.Id = "a";
		summary.Cwd = Core::FromUtf8("D:/Repos/IDE");
		summary.Activity.MainHours.clear();
		HourActivity hour = OpusDollars(dollars);
		hour.Usage[UsageKey{"claude-opus-5", false}].CacheWrite1h = cacheWrites;
		summary.Activity.MainHours[Hour(2026, 11, 9, 9)] = hour;
		const std::vector<const SessionSummary*> sessions{&summary};
		return BuildDashboard(sessions, Pricing::Defaults(), LocalCalendar{nullptr}, Day(2026, 11, 10));
	};

	SessionSummary cheap;
	SessionSummary dear;
	SessionSummary lossy;
	const DashboardModel under = modelCosting(cheap, 999.99, 0);
	const DashboardModel over = modelCosting(dear, 123'456.78, 0);
	const DashboardModel loss = modelCosting(lossy, 1000.0, 900'000'000);
	REQUIRE(loss.Saved < -1000.0);

	for (const float widthInFontSizes : {30.0f, 41.0f, 55.0f, 64.0f, 82.5f, 100.0f})
	{
		CAPTURE(widthInFontSizes);
		std::vector<float> heights;

		for (const DashboardModel* model : {&under, &over, &loss})
		{
			DashboardScreen screen(widthInFontSizes);
			screen.Data.Model = *model;
			screen.DrawFrame();
			screen.DrawFrame();
			const ImGuiWindow* panel = ImGui::FindWindowByName("Panel");
			REQUIRE(panel != nullptr);
			REQUIRE(!panel->DC.ChildWindows.empty());
			heights.push_back(panel->DC.ChildWindows.back()->ContentSize.y);
		}

		CHECK(heights[0] > 0.0f);
		CHECK(heights[1] == heights[0]);
		CHECK(heights[2] == heights[0]);
	}
}

TEST_CASE("At full width the map's total, streak and legend stand beside the grid, below it when narrow")
{
	ImGuiFrames frames;
	ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;
	frames.Next([] {
		const float unit = ImGui::GetFontSize();
		const auto fullWidthMap = [unit](float panelInFontSizes) {
			return LayOutDashboard(panelInFontSizes * unit).Areas[static_cast<std::size_t>(DashboardArea::Map)];
		};

		CHECK(MapSideStandsBeside(fullWidthMap(82.5f).GetWidth()));
		CHECK(MapSideStandsBeside(fullWidthMap(57.5f).GetWidth()));
		CHECK_FALSE(MapSideStandsBeside(fullWidthMap(41.0f).GetWidth()));
		CHECK_FALSE(MapSideStandsBeside(fullWidthMap(20.0f).GetWidth()));
		CHECK_FALSE(MapSideStandsBeside(fullWidthMap(70.0f).GetWidth()));
	});
}

TEST_CASE("Reflows to two and one columns below the breakpoints")
{
	for (const float widthInFontSizes : {50.0f, 30.0f})
	{
		CAPTURE(widthInFontSizes);
		DashboardScreen screen(widthInFontSizes);
		screen.DrawFrame();

		const std::vector<ImRect> tiles = AllTiles(screen.Grid);

		for (std::size_t index = 0; index < tiles.size(); ++index)
		{
			CAPTURE(index);
			CHECK(tiles[index].Min.x >= 0.0f);
			CHECK(tiles[index].Max.x <= screen.Width);
			CHECK(tiles[index].GetWidth() > 0.0f);
			CHECK(tiles[index].GetHeight() > 0.0f);

			for (std::size_t other = index + 1; other < tiles.size(); ++other)
			{
				CAPTURE(other);
				CHECK_FALSE(Overlap(tiles[index], tiles[other]));
			}
		}

		const ImRect& map = AreaOf(screen.Grid, DashboardArea::Map);
		const ImRect& daily = AreaOf(screen.Grid, DashboardArea::Daily);

		if (widthInFontSizes >= kMediumDashboardInFontSizes)
		{
			CHECK(map.Min.y == daily.Min.y);
			CHECK(daily.Min.x > map.Max.x);
		}
		else
		{
			CHECK(map.Min.x == daily.Min.x);
			CHECK(daily.Min.y > map.Max.y);
		}
	}
}

TEST_CASE("At full width the grid follows S: map, daily and models share a row, punch and cache the last")
{
	DashboardScreen screen(kWideDashboardInFontSizes + 10.0f);
	screen.DrawFrame();

	const DashboardGrid& grid = screen.Grid;
	CHECK(grid.Numbers.front().Min.y == grid.Numbers.back().Min.y);
	CHECK(AreaOf(grid, DashboardArea::Map).Min.y == AreaOf(grid, DashboardArea::Models).Min.y);
	CHECK(AreaOf(grid, DashboardArea::Projects).Min.y == AreaOf(grid, DashboardArea::Skills).Min.y);
	CHECK(AreaOf(grid, DashboardArea::Punch).Min.y == AreaOf(grid, DashboardArea::Cache).Min.y);
	CHECK(AreaOf(grid, DashboardArea::Daily).GetWidth() > AreaOf(grid, DashboardArea::Map).GetWidth());
	CHECK(AreaOf(grid, DashboardArea::Punch).GetWidth() > AreaOf(grid, DashboardArea::Cache).GetWidth());
}

TEST_CASE("Uses the family colours")
{
	DashboardScreen screen(kWideDashboardInFontSizes);
	screen.DrawFrame();

	const ImRect daily = screen.OnScreen(AreaOf(screen.Grid, DashboardArea::Daily));
	const ImDrawList& drawing = DashboardScreen::DashboardDrawList();

	CHECK(VerticesInColourWithin(drawing, screen.Colours.Families[static_cast<std::size_t>(ModelFamily::Opus)], daily) > 0);

	for (const ModelFamily family : {ModelFamily::Sonnet, ModelFamily::Haiku, ModelFamily::Fable, ModelFamily::Other})
	{
		CAPTURE(static_cast<int>(family));
		CHECK(VerticesInColourWithin(drawing, screen.Colours.Families[static_cast<std::size_t>(family)], daily) == 0);
	}
}

TEST_CASE("Every list keeps its slots when it holds fewer rows")
{
	DashboardScreen screen(kWideDashboardInFontSizes);
	screen.DrawFrame();

	const ImRect sessions = AreaOf(screen.Grid, DashboardArea::Sessions);
	const ImRect lastSlot = ListRowRect(sessions, kTopRows - 1);
	CHECK(lastSlot.Max.y <= sessions.Max.y);

	const ImRect tools = AreaOf(screen.Grid, DashboardArea::Tools);
	CHECK(ListRowRect(tools, kListRows - 1).Max.y <= tools.Max.y);
}

TEST_CASE("A tip card keeps the widest label of a section whole wherever the card stands")
{
	constexpr int kPlaces = 24;
	constexpr float kFarLeft = 3000.0f;
	constexpr float kStep = 211.37f;

	ImGuiFrames frames;
	ImGui::GetIO().DisplaySize = ImVec2(kFarLeft + kStep * kPlaces + kTallWindow, kTallWindow);
	ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;

	TipCard card;
	card.Head = "Subagents";
	card.HeadValue = "$2447.99";
	card.Sections.push_back({.Rows = {{.Label = "Runs", .Value = "1,032"},
									  {.Label = "Average run", .Value = "$2.37"},
									  {.Label = "Share of the cost", .Value = "55%"}}});
	card.Sections.push_back(
		{.Rows = {{.Label = "Opus", .Value = "$2447.99", .Extra = "100%", .Swatch = ModelFamily::Opus}}});

	std::vector<std::vector<DrawnLine>> cards(kPlaces);
	const auto draw = [&] {
		for (int place = 0; place < kPlaces; ++place)
		{
			ImGui::SetNextWindowPos(ImVec2(kFarLeft + kStep * static_cast<float>(place), 0.0f));
			ImGui::Begin(("Card" + std::to_string(place)).c_str(), nullptr,
						 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
			DrawTipCard(card, DistinctColours());
			cards[static_cast<std::size_t>(place)] =
				DrawnTextLines(*ImGui::GetWindowDrawList(), *ImGui::GetFont()->GetFontBaked(ImGui::GetFontSize()));
			ImGui::End();
		}
	};

	// WARNING: Two frames, so every glyph is in the atlas before the second one's quads are read back.
	frames.Next(draw);
	frames.Next(draw);

	for (const std::vector<DrawnLine>& lines : cards)
	{
		const auto has = [&lines](std::string_view text) {
			return std::ranges::any_of(lines, [text](const DrawnLine& line) { return line.Text == text; });
		};

		CHECK(has("Shareofthecost55%"));
		CHECK(has("Subagents$2447.99"));
		CHECK(has("Opus$2447.99100%"));
	}
}

TEST_CASE("A tip card's tooltip is edged in the frame colour")
{
	ImGuiFrames frames;
	ImGui::GetStyle().PopupBorderSize = 1.0f;
	const DashboardColours colours = DistinctColours();
	TipCard card;
	card.Head = "Bash";
	card.HeadValue = "28,661 calls";

	int edgeVertices = 0;
	frames.Next([&] {
		if (BeginTipCard(colours))
		{
			DrawTipCard(card, colours);
			ImGui::EndTooltip();
		}

		const ImGuiWindow* tooltip = ShownTooltip();
		REQUIRE(tooltip != nullptr);
		edgeVertices = VerticesInColourWithin(*tooltip->DrawList, colours.Frame, tooltip->Rect());
		CHECK(ImGui::GetColorU32(ImGuiCol_Border) != colours.Frame);
	});

	CHECK(edgeVertices > 0);
}

TEST_CASE("On a light theme the tip card is lighter than the tiles and the empty day darker")
{
	ImGuiFrames frames;
	frames.Next([] {
		SessionPalette palette;
		palette.Run = IM_COL32(40, 120, 40, 255);
		const ImU32 surface = IM_COL32(236, 236, 236, 255);

		const DashboardColours colours = DashboardColoursFrom(palette, surface);

		CHECK(ChannelOf(colours.TipSurface, IM_COL32_R_SHIFT) > ChannelOf(surface, IM_COL32_R_SHIFT));
		CHECK(ChannelOf(colours.Heat[0], IM_COL32_R_SHIFT) < ChannelOf(surface, IM_COL32_R_SHIFT));
		CHECK(colours.Heat[4] == palette.Run);
	});
}

TEST_CASE("A tip card has the tip surface, and a shadow around it that never covers it")
{
	ImGuiFrames frames;
	ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;
	DashboardColours colours = DistinctColours();
	colours.TipSurface = IM_COL32(13, 14, 16, 255);
	colours.Shadow = IM_COL32(0, 0, 0, 128);
	TipCard card;
	card.Head = "Bash";
	card.HeadValue = "28,661 calls";

	ImRect edge;
	int backgroundVertices = 0;
	const auto draw = [&] {
		if (BeginTipCard(colours))
		{
			DrawTipCard(card, colours);
			const ImGuiWindow* tooltip = ImGui::GetCurrentWindowRead();
			edge = tooltip->Rect();
			backgroundVertices = VerticesInColourWithin(*tooltip->DrawList, colours.TipSurface, edge);
			EndTipCard(colours);
		}
	};
	frames.Next(draw);
	frames.Next(draw);

	CHECK(backgroundVertices > 0);
	const ImDrawList& foreground = *ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
	int outside = 0;
	int inside = 0;

	for (const ImDrawCmd& command : foreground.CmdBuffer)
	{
		for (unsigned int index = command.IdxOffset; index < command.IdxOffset + command.ElemCount; ++index)
		{
			const ImDrawVert& vertex = foreground.VtxBuffer[foreground.IdxBuffer[index]];

			if ((vertex.col & IM_COL32_A_MASK) == 0 || (vertex.col & ~IM_COL32_A_MASK) != 0)
			{
				continue;
			}

			const ImVec4 clip = command.ClipRect;
			const bool clipsTheCard = clip.x < edge.Max.x && edge.Min.x < clip.z && clip.y < edge.Max.y && edge.Min.y < clip.w;
			(clipsTheCard ? inside : outside) += 1;
		}
	}

	CHECK(outside > 0);
	CHECK(inside == 0);
}

TEST_CASE("A long body stops at four lines")
{
	ImGuiFrames frames;
	ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;
	TipCard card;
	card.Body = "word";
	for (int index = 0; index < 300; ++index)
	{
		card.Body += " word";
	}

	std::vector<DrawnLine> lines;
	const auto draw = [&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::Begin("Body", nullptr,
					 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
		DrawTipCard(card, DistinctColours());
		lines = DrawnTextLines(*ImGui::GetWindowDrawList(), *ImGui::GetFont()->GetFontBaked(ImGui::GetFontSize()));
		ImGui::End();
	};
	frames.Next(draw);
	frames.Next(draw);

	CHECK(lines.size() == 4);
}

TEST_CASE("DashboardColoursFrom maps the palette as decided, with rules and edges lifted off the surface")
{
	ImGuiFrames frames;
	frames.Next([] {
		SessionPalette palette;
		palette.Accent = IM_COL32(226, 176, 74, 255);
		palette.Read = IM_COL32(90, 167, 224, 255);
		palette.Edit = IM_COL32(229, 192, 123, 255);
		palette.Run = IM_COL32(140, 194, 101, 255);
		palette.Agent = IM_COL32(180, 140, 224, 255);
		palette.Other = IM_COL32(150, 150, 150, 255);
		palette.TextDisabled = IM_COL32(110, 110, 110, 255);
		palette.Failed = IM_COL32(215, 154, 138, 255);
		const ImU32 surface = IM_COL32(35, 36, 40, 255);

		const DashboardColours colours = DashboardColoursFrom(palette, surface);

		CHECK(colours.Families[static_cast<std::size_t>(ModelFamily::Opus)] == palette.Accent);
		CHECK(colours.Families[static_cast<std::size_t>(ModelFamily::Sonnet)] == palette.Read);
		CHECK(colours.Families[static_cast<std::size_t>(ModelFamily::Haiku)] == palette.Edit);
		CHECK(colours.Families[static_cast<std::size_t>(ModelFamily::Fable)] == palette.Agent);
		CHECK(colours.Families[static_cast<std::size_t>(ModelFamily::Other)] == palette.Other);
		CHECK(colours.Good == palette.Run);
		CHECK(colours.Heat[4] == palette.Run);
		CHECK(ShareFromTo(colours.Heat[1], surface, palette.Run) == doctest::Approx(0.3f).epsilon(0.03));
		CHECK(ShareFromTo(colours.Bar, surface, palette.Run) == doctest::Approx(0.18f).epsilon(0.05));
		CHECK(ChannelOf(colours.TipSurface, IM_COL32_R_SHIFT) < ChannelOf(surface, IM_COL32_R_SHIFT));
		CHECK(ChannelOf(colours.Shadow, IM_COL32_A_SHIFT) > 0);
		CHECK(ShareFromTo(colours.Pill, surface, palette.Accent) == doctest::Approx(0.15f).epsilon(0.1));
		CHECK(ChannelOf(colours.Heat[0], IM_COL32_R_SHIFT) > ChannelOf(surface, IM_COL32_R_SHIFT));
		CHECK((colours.Hatch & ~IM_COL32_A_MASK) == (palette.TextDisabled & ~IM_COL32_A_MASK));
		CHECK(ChannelOf(colours.Hatch, IM_COL32_A_SHIFT) == 89);
		CHECK(colours.Tile == surface);
		CHECK(ChannelOf(colours.Frame, IM_COL32_R_SHIFT) > ChannelOf(colours.Heat[0], IM_COL32_R_SHIFT));
	});
}
