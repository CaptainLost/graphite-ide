#include <doctest/doctest.h>

#include "DrawnText.h"
#include "ImGuiFrames.h"

#include "Sessions/Overview/DashboardCanvas.h"
#include "Sessions/Overview/DashboardSource.h"
#include "Sessions/Overview/DashboardView.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/FontSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace Graphite;

namespace {
	constexpr std::uint64_t kOneDollarOfOpusInput = 200'000;
	constexpr float kNarrowDashboardInFontSizes = 30.0f;
	constexpr float kTallWindow = 2000.0f;

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

	SessionSummary Costing(const char* file, const char* title, const char* cwd, double dollars)
	{
		SessionSummary summary;
		summary.File = Core::FromUtf8(file);
		summary.Id = file;
		summary.Title = title;
		summary.Cwd = Core::FromUtf8(cwd);
		summary.StartedAt = "2026-11-10T09:00:00.000Z";
		summary.EndedAt = "2026-11-10T10:00:00.000Z";
		summary.Activity.MainHours[Hour(2026, 11, 10, 9)] = OpusDollars(dollars);
		summary.Activity.MainUsageByCwd[cwd] = summary.Activity.MainHours.begin()->second.Usage;
		return summary;
	}

	void Put(SessionsByFile& sessions, SessionSummary summary)
	{
		const std::filesystem::path file = summary.File;
		sessions[file] = std::move(summary);
	}

	// Every colour opaque, since text in a transparent colour draws nothing to read back.
	DashboardColours OpaqueColours()
	{
		const ImU32 grey = IM_COL32(200, 200, 200, 255);
		DashboardColours colours{};
		colours.Families.fill(grey);
		colours.Heat.fill(grey);
		colours.Hatch = colours.Bar = colours.Ink = colours.Dim = colours.Tile = colours.Frame = grey;
		colours.Accent = colours.Bad = colours.Good = grey;
		return colours;
	}

	// The rows of the expensive-session list as drawn: the lines inside its tile that hold a cost.
	struct ExpensiveList
	{
		ImRect Area;
		std::vector<std::string> Rows;
	};

	ExpensiveList DrawExpensiveList(const DashboardModel& model, const LocalCalendar& calendar)
	{
		ImGuiFrames frames;
		ImGui::GetIO().DisplaySize = ImVec2(kTallWindow, kTallWindow);
		ImGuiStyle& style = ImGui::GetStyle();
		style.FontSizeBase = Core::kInterfaceFontSize;
		style.WindowPadding = ImVec2(0.0f, 0.0f);
		const float width = kNarrowDashboardInFontSizes * Core::kInterfaceFontSize;
		const DashboardColours colours = OpaqueColours();
		ExpensiveList list;

		frames.Next([&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(width, kTallWindow));
			ImGui::Begin("ReplacedSession", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			DrawDashboard(model, calendar, colours);
			const ImRect area = LayOutDashboard(model, width).Areas[static_cast<std::size_t>(DashboardArea::Sessions)];
			list.Area = ImRect(ImVec2(area.Min.x + origin.x, area.Min.y + origin.y),
							   ImVec2(area.Max.x + origin.x, area.Max.y + origin.y));

			const ImGuiWindow* panel = ImGui::GetCurrentWindowRead();
			REQUIRE(!panel->DC.ChildWindows.empty());
			const std::vector<DrawnLine> lines =
				DrawnTextLines(*panel->DC.ChildWindows.back()->DrawList,
							   *ImGui::GetFont()->GetFontBaked(ImGui::GetFontSize() * kBodySize));

			for (const DrawnLine& line : lines)
			{
				const bool holdsACost = line.Text.find('$') != std::string::npos;

				if (holdsACost && line.Top >= list.Area.Min.y && line.Top < list.Area.Max.y)
				{
					list.Rows.push_back(line.Text);
				}
			}

			ImGui::End();
		});

		return list;
	}
}

TEST_CASE("Top lists keep their row count when a session is replaced")
{
	SessionsByFile sessions;
	Put(sessions, Costing("C:\\t\\large.jsonl", "Large", "D:\\Repos\\IDE", 3.0));
	Put(sessions, Costing("C:\\t\\small.jsonl", "Small", "D:\\Repos\\Notes", 1.0));

	const LocalCalendar calendar{nullptr};
	const Pricing pricing = Pricing::Defaults();
	DashboardSource source;
	REQUIRE(source.Refresh(sessions, pricing, {}, calendar, Day(2026, 11, 10)));

	const ExpensiveList before = DrawExpensiveList(source.GetModel(), calendar);
	REQUIRE(before.Rows.size() == 2);
	CHECK(before.Rows.front().starts_with("Large"));

	Put(sessions, Costing("C:\\t\\small.jsonl", "Grown", "D:\\Repos\\Notes", 5.0));
	source.MarkChanged();
	REQUIRE(source.Refresh(sessions, pricing, {}, calendar, Day(2026, 11, 10)));

	const ExpensiveList after = DrawExpensiveList(source.GetModel(), calendar);
	CHECK(after.Rows.size() == before.Rows.size());
	CHECK(after.Area.GetHeight() == before.Area.GetHeight());
	REQUIRE(!after.Rows.empty());
	CHECK(after.Rows.front().starts_with("Grown"));
}

TEST_CASE("The dashboard is rebuilt only when the summaries, the prices, the folder or the day change")
{
	SessionsByFile sessions;
	Put(sessions, Costing("C:\\t\\one.jsonl", "One", "D:\\Repos\\IDE", 1.0));

	const LocalCalendar calendar{nullptr};
	Pricing pricing = Pricing::Defaults();
	const std::chrono::year_month_day today = Day(2026, 11, 10);
	DashboardSource source;

	CHECK(source.Refresh(sessions, pricing, {}, calendar, today));
	CHECK_FALSE(source.Refresh(sessions, pricing, {}, calendar, today));

	source.MarkChanged();
	CHECK(source.Refresh(sessions, pricing, {}, calendar, today));
	CHECK_FALSE(source.Refresh(sessions, pricing, {}, calendar, today));

	pricing.Set("claude-opus-5", Price{.Input = 1.0});
	CHECK(source.Refresh(sessions, pricing, {}, calendar, today));
	CHECK_FALSE(source.Refresh(sessions, pricing, {}, calendar, today));

	const std::filesystem::path folder = Core::FromUtf8("D:\\Repos\\IDE");
	CHECK(source.Refresh(sessions, pricing, folder, calendar, today));
	CHECK_FALSE(source.Refresh(sessions, pricing, folder, calendar, today));

	CHECK(source.Refresh(sessions, pricing, folder, calendar, Day(2026, 11, 11)));
}

TEST_CASE("With a folder, only sessions whose place is under it are kept, worktrees of it included")
{
	SessionsByFile sessions;
	Put(sessions, Costing("C:\\t\\main.jsonl", "Main", "D:\\Repos\\IDE", 1.0));
	Put(sessions, Costing("C:\\t\\fox.jsonl", "Fox", "D:\\Repos\\IDE\\.claude\\worktrees\\fox", 1.0));
	Put(sessions, Costing("C:\\t\\notes.jsonl", "Notes", "D:\\Repos\\Notes", 1.0));

	const LocalCalendar calendar{nullptr};
	DashboardSource source;
	source.Refresh(sessions, Pricing::Defaults(), Core::FromUtf8("D:\\Repos\\IDE"), calendar, Day(2026, 11, 10));

	std::vector<std::string> titles;
	for (const ListedSession& session : source.GetSessions())
	{
		titles.push_back(session.Summary->Title);
		CHECK(session.ModelIndex.has_value());
	}

	std::ranges::sort(titles);
	CHECK(titles == std::vector<std::string>{"Fox", "Main"});
	CHECK(source.GetModel().Sessions.size() == 2);
	CHECK(source.Stamp() == "from 2 transcripts \xC2\xB7 10 Nov");

	source.Refresh(sessions, Pricing::Defaults(), {}, calendar, Day(2026, 11, 10));
	CHECK(source.GetSessions().size() == 3);
}
