#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "DrawnText.h"
#include "ImGuiFrames.h"

#include "Sessions/LocalClock.h"
#include "Sessions/Overview/DashboardSource.h"
#include "Sessions/SessionList.h"
#include "UI/Lists/ListRow.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/FontSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

using namespace Graphite;

namespace {

	constexpr float kFringe = 1.5f;
	constexpr float kHighlightInset = 6.0f;
	constexpr float kAwayFromTheRight = 20.0f;
	constexpr int kRowCount = 2;
	constexpr int kFirstRow = 0;
	constexpr int kSecondRow = 1;
	constexpr float kInterfaceItemSpacing = 5.0f;
	constexpr float kListBodyItemSpacing = 3.0f;

	struct Extent
	{
		float Left = FLT_MAX;
		float Right = -FLT_MAX;
		float Top = FLT_MAX;
		float Bottom = -FLT_MAX;
	};

	std::optional<Extent> ExtentInColour(const ImDrawList& drawing, ImU32 colour, float fromY = -FLT_MAX,
										 float toY = FLT_MAX)
	{
		std::optional<Extent> extent;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col != colour || vertex.pos.y < fromY || vertex.pos.y > toY)
			{
				continue;
			}

			if (!extent.has_value())
			{
				extent = Extent{};
			}

			extent->Left = std::min(extent->Left, vertex.pos.x);
			extent->Right = std::max(extent->Right, vertex.pos.x);
			extent->Top = std::min(extent->Top, vertex.pos.y);
			extent->Bottom = std::max(extent->Bottom, vertex.pos.y);
		}

		return extent;
	}

	bool WithinFringe(float drawn, float expected)
	{
		return std::abs(drawn - expected) <= kFringe;
	}

	// The sessions list as the panel holds it: a list body in a child window with no padding of its own, under the
	// interface's item spacing, one row per session.
	struct SessionListScreen
	{
		ImGuiFrames Frames;
		ImRect List;
		float RowTops[kRowCount] = {};
		float DetailTops[kRowCount] = {};
		float Pitch = 0.0f;
		float TextLine = 0.0f;

		SessionListScreen()
		{
			ImGuiStyle& style = ImGui::GetStyle();
			style.FontSizeBase = Core::kInterfaceFontSize;
			style.ItemSpacing.y = kInterfaceItemSpacing;
		}

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f));
			ImGui::Begin("Panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			ImGui::BeginChild("##sessionList");

			{
				const ListBodyScope listBody;
				List = ImGui::GetCurrentWindowRead()->InnerRect;
				Pitch = ListRowPitch();
				TextLine = ImGui::GetTextLineHeight();

				for (int row = 0; row < kRowCount; ++row)
				{
					ImGui::PushID(row);
					RowTops[row] = ImGui::GetCursorScreenPos().y;
					DrawSessionRow(SessionRowText{.Title = "A session", .Detail = "$0.10  5 min"});
					DetailTops[row] = ImGui::GetItemRectMin().y;
					ImGui::PopID();
				}
			}

			ImGui::EndChild();
			ImGui::End();
		}

		void DrawFrame()
		{
			Frames.Next([this] { Draw(); });
		}

		static const ImDrawList& DrawList()
		{
			const ImGuiWindow* panel = ImGui::FindWindowByName("Panel");
			REQUIRE(panel != nullptr);
			REQUIRE(!panel->DC.ChildWindows.empty());
			return *panel->DC.ChildWindows.back()->DrawList;
		}
	};
}

TEST_CASE("The sessions list draws through ListRowSelectable: its row pitch equals ListRowPitch()")
{
	SessionListScreen screen;
	screen.DrawFrame();

	ImGui::GetIO().AddMousePosEvent(screen.List.Max.x - kAwayFromTheRight, screen.RowTops[kSecondRow] + screen.Pitch * 0.5f);
	screen.DrawFrame();

	const ImU32 hoveredColour = ImGui::GetColorU32(CurrentListRowColours().Hovered);
	const std::optional<Extent> hovered = ExtentInColour(SessionListScreen::DrawList(), hoveredColour);
	REQUIRE(hovered.has_value());

	CHECK(WithinFringe(hovered->Top, screen.RowTops[kSecondRow]));
	CHECK(WithinFringe(hovered->Bottom - hovered->Top, screen.Pitch));
	CHECK(WithinFringe(hovered->Left, screen.List.Min.x + kHighlightInset));
	CHECK(WithinFringe(hovered->Right, screen.List.Max.x - kHighlightInset));
	CHECK_FALSE(DrawsInColour(SessionListScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_HeaderHovered)));

	const std::optional<Extent> title = ExtentInColour(SessionListScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_Text),
													   hovered->Top, hovered->Bottom);
	REQUIRE(title.has_value());
	CHECK(title->Left > hovered->Left);

	const std::optional<Extent> details = ExtentInColour(SessionListScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_TextDisabled),
														 screen.RowTops[kFirstRow], screen.RowTops[kSecondRow]);
	REQUIRE(details.has_value());
	CHECK(details->Top >= screen.RowTops[kFirstRow] + screen.Pitch);
}

TEST_CASE("A session's detail line starts at its row's foot and the next session one line and the list's spacing below")
{
	SessionListScreen screen;
	screen.DrawFrame();

	CHECK(screen.DetailTops[kFirstRow] - screen.RowTops[kFirstRow] == doctest::Approx(screen.Pitch));
	CHECK(screen.RowTops[kSecondRow] - screen.DetailTops[kFirstRow] ==
		  doctest::Approx(screen.TextLine + kListBodyItemSpacing));
}

namespace {

	std::chrono::year_month_day Day(int year, unsigned month, unsigned day)
	{
		return std::chrono::year{year} / month / day;
	}

	UtcHour Hour(int year, unsigned month, unsigned day, int hour)
	{
		return std::chrono::sys_days{Day(year, month, day)} + std::chrono::hours{hour};
	}

	HourActivity OneRequest()
	{
		HourActivity activity;
		const UsageKey key{"claude-opus-5", false};
		activity.Usage[key] = {1000, 0, 0, 0, 0};
		activity.Requests = 1;
		activity.RequestsByModel[key] = 1;
		return activity;
	}

	SessionSummary& AddSession(SessionsByFile& sessions, const char* file, const char* title, const char* cwd)
	{
		SessionSummary& summary = sessions[Core::FromUtf8(file)];
		summary.File = Core::FromUtf8(file);
		summary.Id = file;
		summary.Title = title;
		summary.Cwd = Core::FromUtf8(cwd);
		summary.StartedAt = "2026-11-10T09:00:00.000Z";
		summary.EndedAt = "2026-11-10T10:00:00.000Z";
		return summary;
	}

	bool NeverOpen(const SessionSummary&)
	{
		return false;
	}
}

TEST_CASE("ActiveOnDay is true only for a local day with usage")
{
	const LocalCalendar warsaw{std::chrono::locate_zone("Europe/Warsaw")};

	SessionSummary main;
	main.Activity.MainHours[Hour(2026, 10, 24, 22)] = OneRequest();

	CHECK(ActiveOnDay(main, Day(2026, 10, 25), warsaw));
	CHECK_FALSE(ActiveOnDay(main, Day(2026, 10, 24), warsaw));

	SessionSummary subagent;
	subagent.Activity.SubagentHours[Hour(2026, 10, 24, 21)] = OneRequest();

	CHECK(ActiveOnDay(subagent, Day(2026, 10, 24), warsaw));
	CHECK_FALSE(ActiveOnDay(subagent, Day(2026, 10, 25), warsaw));
}

TEST_CASE("DayChipText names the day and how many sessions were active on it")
{
	CHECK(DayChipText(Day(2026, 10, 11), 4) == "Sun 11 Oct \xC2\xB7 4");
}

TEST_CASE("A worktree session sits under its repository with the worktree's name beside its title")
{
	SessionsByFile sessions;
	AddSession(sessions, "C:\\t\\main.jsonl", "Draw the list", "D:\\Repos\\IDE");
	AddSession(sessions, "C:\\t\\fox.jsonl", "Build the chips", "D:\\Repos\\IDE\\.claude\\worktrees\\fox");

	const LocalCalendar calendar{nullptr};
	const Pricing pricing = Pricing::Defaults();
	DashboardSource source;
	source.Refresh(sessions, pricing, {}, calendar, Day(2026, 11, 10));

	const std::vector<SessionListRow> rows =
		SessionListRows(source.GetSessions(), source.GetModel(), SessionListFilter{.Grouped = true}, NeverOpen, calendar);
	REQUIRE(rows.size() == 2);

	ImGuiFrames frames;
	ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;
	LocalClock clock;
	const DashboardColours colours{};
	std::vector<DrawnLine> lines;

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
		ImGui::Begin("WorktreeList", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
		const SessionListContext context{
			.Prices = pricing, .Model = source.GetModel(), .Calendar = calendar, .Colours = colours, .Clock = clock};
		DrawSessionList(rows, true, context);
		const ImGuiWindow* panel = ImGui::GetCurrentWindowRead();
		REQUIRE(!panel->DC.ChildWindows.empty());
		lines = DrawnTextLines(*panel->DC.ChildWindows.back()->DrawList,
							   *ImGui::GetFont()->GetFontBaked(ImGui::GetFontSize()));
		ImGui::End();
	});

	const auto holding = [&](std::string_view text) {
		return std::count_if(lines.begin(), lines.end(),
							 [&](const DrawnLine& line) { return line.Text.find(text) != std::string::npos; });
	};

	CHECK(holding("D:\\Repos\\IDE") == 1);
	CHECK(holding("worktrees") == 0);

	// The header's chevron is an icon the test's font has no glyph for, so its line does not start with the path.
	const auto headerLine = std::ranges::find_if(
		lines, [](const DrawnLine& line) { return line.Text.find("D:\\Repos\\IDE") != std::string::npos; });
	REQUIRE(headerLine != lines.end());
	const std::optional<std::size_t> header = static_cast<std::size_t>(headerLine - lines.begin());
	const std::optional<std::size_t> worktree = LineStartingWith(lines, "Buildthechips");
	const std::optional<std::size_t> main = LineStartingWith(lines, "Drawthelist");
	REQUIRE(header.has_value());
	REQUIRE(worktree.has_value());
	REQUIRE(main.has_value());
	CHECK(*header < *worktree);
	CHECK(*header < *main);
	CHECK(lines[*worktree].Text.starts_with("Buildthechips"));
	CHECK(lines[*worktree].Text.ends_with("fox"));
	CHECK(lines[*main].Text == "Drawthelist");
}

TEST_CASE("A long worktree name takes at most two fifths of the row and the title keeps the rest")
{
	constexpr float kWidth = 400.0f;
	constexpr ImU32 kWorktreeColour = IM_COL32(1, 2, 3, 255);
	ImGuiFrames frames;
	ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;
	ImRect row;

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(kWidth, 200.0f));
		ImGui::Begin("LongWorktree", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
		{
			const ListBodyScope listBody;
			DrawSessionRow({.Title = "A title long enough to need cutting when the worktree beside it is long too",
							.Worktree = "a-worktree-name-much-too-long-to-be-shown-whole-in-the-row",
							.Detail = "$1.00",
							.WorktreeColour = kWorktreeColour});
			row = ImRect(ImGui::GetCurrentWindowRead()->InnerRect.Min, ImVec2(kWidth, ImGui::GetItemRectMin().y));
		}
		ImGui::End();
	});

	const ImDrawList& drawing = *ImGui::FindWindowByName("LongWorktree")->DrawList;
	const std::optional<Extent> worktree = ExtentInColour(drawing, kWorktreeColour, row.Min.y, row.Max.y);
	const std::optional<Extent> title = ExtentInColour(drawing, ImGui::GetColorU32(ImGuiCol_Text), row.Min.y, row.Max.y);
	REQUIRE(worktree.has_value());
	REQUIRE(title.has_value());
	CHECK(worktree->Right - worktree->Left <= row.GetWidth() * 0.4f + kFringe);
	CHECK(title->Right < worktree->Left);
	CHECK(title->Right - title->Left > row.GetWidth() * 0.4f);
}

TEST_CASE("The day chip is an accent pill, and a click on it answers true")
{
	ImGuiFrames frames;
	ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize;
	DashboardColours colours{};
	colours.Pill = IM_COL32(58, 50, 32, 255);
	colours.Accent = IM_COL32(226, 176, 74, 255);
	ImRect chip;
	bool clicked = false;

	const auto draw = [&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
		ImGui::Begin("Chip", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
		clicked = DrawDayChip(DayChipText(Day(2026, 10, 11), 3), colours) || clicked;
		chip = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		ImGui::End();
	};
	frames.Next(draw);

	const ImDrawList& drawing = *ImGui::FindWindowByName("Chip")->DrawList;
	const std::optional<Extent> pill = ExtentInColour(drawing, colours.Pill);
	const std::optional<Extent> text = ExtentInColour(drawing, colours.Accent);
	REQUIRE(pill.has_value());
	REQUIRE(text.has_value());
	CHECK(WithinFringe(pill->Left, chip.Min.x));
	CHECK(WithinFringe(pill->Right, chip.Max.x));
	CHECK(text->Left > pill->Left);
	CHECK(text->Right < pill->Right);
	CHECK_FALSE(clicked);

	frames.Click(chip.GetCenter(), draw);
	CHECK(clicked);
}

TEST_CASE("A day filter keeps only the sessions active on that day")
{
	SessionsByFile sessions;
	AddSession(sessions, "C:\\t\\monday.jsonl", "Monday", "D:\\Repos\\IDE").Activity.MainHours[Hour(2026, 11, 9, 10)] =
		OneRequest();
	AddSession(sessions, "C:\\t\\tuesday.jsonl", "Tuesday", "D:\\Repos\\IDE").Activity.MainHours[Hour(2026, 11, 10, 10)] =
		OneRequest();

	const LocalCalendar calendar{nullptr};
	DashboardSource source;
	source.Refresh(sessions, Pricing::Defaults(), {}, calendar, Day(2026, 11, 10));

	const std::vector<SessionListRow> rows = SessionListRows(
		source.GetSessions(), source.GetModel(), SessionListFilter{.Day = Day(2026, 11, 9), .Grouped = true}, NeverOpen,
		calendar);

	REQUIRE(rows.size() == 1);
	CHECK(rows.front().Session->Summary->Title == "Monday");

	const std::vector<SessionListRow> titled = SessionListRows(
		source.GetSessions(), source.GetModel(),
		SessionListFilter{.Title = "nothing like it", .Day = Day(2026, 11, 9), .Grouped = true}, NeverOpen, calendar);
	CHECK(titled.empty());
}
