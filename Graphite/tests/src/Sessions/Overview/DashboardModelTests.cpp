#include <doctest/doctest.h>

#include "Sessions/Overview/DashboardFigures.h"
#include "Sessions/Overview/DashboardModel.h"

#include "Core/FileSystem/FileSystem.h"

#include <vector>

using namespace Graphite;

namespace {
	constexpr std::uint64_t kOneDollarOfOpusInput = 200'000;

	std::chrono::year_month_day Day(int year, unsigned month, unsigned day)
	{
		return std::chrono::year{year} / month / day;
	}

	UtcHour Hour(int year, unsigned month, unsigned day, int hour)
	{
		return std::chrono::sys_days{Day(year, month, day)} + std::chrono::hours{hour};
	}

	HourActivity Activity(const char* model, TokenCounts counts, std::size_t requests, std::size_t prompts = 0)
	{
		HourActivity activity;
		activity.Usage[UsageKey{model, false}] = counts;
		activity.Requests = requests;
		activity.RequestsByModel[UsageKey{model, false}] = requests;
		activity.Prompts = prompts;
		return activity;
	}

	HourActivity OpusDollars(double dollars, std::size_t requests = 1)
	{
		const auto input = static_cast<std::uint64_t>(dollars * static_cast<double>(kOneDollarOfOpusInput));
		return Activity("claude-opus-5", {input, 0, 0, 0, 0}, requests);
	}

	SessionSummary Session(const char* id, const char* cwd = "D:\\Repos\\IDE")
	{
		SessionSummary summary;
		summary.Id = id;
		summary.Cwd = Core::FromUtf8(cwd);
		return summary;
	}

	DashboardModel Build(const std::vector<const SessionSummary*>& sessions, std::chrono::year_month_day today,
						 const LocalCalendar& calendar = LocalCalendar{nullptr})
	{
		return BuildDashboard(sessions, Pricing::Defaults(), calendar, today);
	}

	std::size_t Family(ModelFamily family)
	{
		return static_cast<std::size_t>(family);
	}
}

TEST_CASE("Models are grouped into families by name")
{
	CHECK(FamilyOf("claude-opus-5") == ModelFamily::Opus);
	CHECK(FamilyOf("claude-opus-4-8-20260101") == ModelFamily::Opus);
	CHECK(FamilyOf("claude-sonnet-5") == ModelFamily::Sonnet);
	CHECK(FamilyOf("claude-haiku-4-5") == ModelFamily::Haiku);
	CHECK(FamilyOf("claude-fable-5-1") == ModelFamily::Fable);
	CHECK(FamilyOf("claude-mythos-5") == ModelFamily::Other);
	CHECK(FamilyOf("<synthetic>") == ModelFamily::Other);
	CHECK(FamilyOf("claude-opusx") == ModelFamily::Other);
	CHECK(FamilyName(ModelFamily::Opus) == "Opus");
	CHECK(FamilyName(ModelFamily::Fable) == "Fable");
	CHECK(FamilyName(ModelFamily::Other) == "Other");
}

TEST_CASE("CacheSaving is net of the write premium")
{
	const Pricing pricing = Pricing::Defaults();
	const TokenCounts counts{0, 0, 100'000, 0, 1'000'000};

	CHECK(CacheSaving({"claude-opus-5", false}, counts, pricing) == doctest::Approx(4.375));
	CHECK(CacheSaving({"claude-opus-5", true}, counts, pricing) == doctest::Approx(8.75));
	CHECK(CacheSaving({"claude-unknown-9", false}, counts, pricing) == 0.0);
	CHECK(CacheSaving({"claude-opus-5", false}, {0, 0, 0, 100'000, 0}, pricing) == doctest::Approx(-0.5));
}

TEST_CASE("Days run from the oldest usage to today without gaps")
{
	SessionSummary summary = Session("a");
	summary.Activity.MainHours[Hour(2026, 11, 1, 10)] = OpusDollars(2);
	summary.Activity.MainHours[Hour(2026, 11, 4, 10)] = OpusDollars(3);

	const DashboardModel model = Build({&summary}, Day(2026, 11, 5));

	REQUIRE(model.Days.size() == 5);
	CHECK(model.Days.front().Day == Day(2026, 11, 1));
	CHECK(model.Days[1].Day == Day(2026, 11, 2));
	CHECK(model.Days[1].Cost == 0.0);
	CHECK(model.Days[3].Cost == doctest::Approx(3.0));
	CHECK(model.Days.back().Day == Day(2026, 11, 5));
	CHECK(model.Cost == doctest::Approx(5.0));
	CHECK(model.Today == Day(2026, 11, 5));
}

TEST_CASE("Days stay within five years before today and tomorrow, whatever hours a summary holds")
{
	SessionSummary summary = Session("a");
	summary.Activity.MainHours[Hour(1970, 1, 1, 0)] = OpusDollars(1);
	summary.Activity.MainHours[Hour(1, 1, 1, 0)] = OpusDollars(1);
	summary.Activity.MainHours[Hour(9999, 12, 31, 23)] = OpusDollars(1);
	summary.Activity.MainHours[Hour(2026, 11, 9, 10)] = OpusDollars(1);

	const DashboardModel model = Build({&summary}, Day(2026, 11, 10));

	REQUIRE(!model.Days.empty());
	CHECK(model.Days.front().Day >= Day(2021, 11, 9));
	CHECK(model.Days.back().Day == Day(2026, 11, 11));
	CHECK(model.Days.size() <= 5 * 366 + 2);
}

TEST_CASE("A session's hours go to the local day")
{
	const LocalCalendar warsaw{std::chrono::locate_zone("Europe/Warsaw")};
	SessionSummary summary = Session("a");
	summary.StartedAt = "2026-10-24T22:30:00.000Z";
	summary.Activity.MainHours[Hour(2026, 10, 24, 22)] = OpusDollars(1);

	const DashboardModel model = Build({&summary}, Day(2026, 10, 25), warsaw);

	REQUIRE(model.Days.size() == 1);
	CHECK(model.Days[0].Day == Day(2026, 10, 25));
	CHECK(model.Days[0].SessionsStarted == 1);
	REQUIRE(model.Sessions.size() == 1);
	CHECK(model.Sessions[0].CostByDay.contains(Day(2026, 10, 25)));
	CHECK(model.Punch[6][0].Cost == doctest::Approx(1.0));
	CHECK(model.Punch[6][0].Requests == 1);
}

TEST_CASE("Subagent hours count as subagent cost and in the family totals")
{
	SessionSummary summary = Session("a");
	summary.SubagentCount = 2;
	summary.Activity.MainHours[Hour(2026, 11, 3, 9)] = Activity("claude-opus-5", {1'000'000, 0, 0, 0, 0}, 2, 1);
	summary.Activity.SubagentHours[Hour(2026, 11, 3, 9)] = Activity("claude-haiku-4-5", {1'000'000, 1000, 0, 0, 0}, 3);

	const DashboardModel model = Build({&summary}, Day(2026, 11, 3));

	CHECK(model.Cost == doctest::Approx(6.005));
	CHECK(model.SubagentCost == doctest::Approx(1.005));
	CHECK(model.Requests == 5);
	CHECK(model.Prompts == 1);
	CHECK(model.SubagentRuns == 2);
	CHECK(model.Tokens.Input == 2'000'000);
	CHECK(model.Tokens.Output == 1000);

	const FamilyTotals& haiku = model.Families[Family(ModelFamily::Haiku)];
	CHECK(haiku.Cost == doctest::Approx(1.005));
	CHECK(haiku.SubagentCost == doctest::Approx(1.005));
	CHECK(haiku.Requests == 3);
	CHECK(haiku.Tokens.Output == 1000);

	const FamilyTotals& opus = model.Families[Family(ModelFamily::Opus)];
	CHECK(opus.Cost == doctest::Approx(5.0));
	CHECK(opus.SubagentCost == 0.0);
	CHECK(opus.Requests == 2);

	REQUIRE(model.Days.size() == 1);
	CHECK(model.Days[0].SubagentCost == doctest::Approx(1.005));
	CHECK(model.Days[0].CostByFamily[Family(ModelFamily::Haiku)] == doctest::Approx(1.005));
	CHECK(model.Days[0].Requests == 5);
	CHECK(model.Days[0].Prompts == 1);
	CHECK(model.Days[0].Output == 1000);

	REQUIRE(model.Sessions.size() == 1);
	CHECK(model.Sessions[0].SubagentCost == doctest::Approx(1.005));
	CHECK(model.Sessions[0].Requests == 5);
}

TEST_CASE("An hour with two models gives each family exactly its own requests")
{
	SessionSummary summary = Session("a");
	HourActivity hour;
	hour.Usage[{"claude-opus-5", false}] = {300, 0, 0, 0, 0};
	hour.Usage[{"claude-haiku-4-5", false}] = {100, 0, 0, 0, 0};
	hour.RequestsByModel = {{{"claude-opus-5", false}, 1}, {{"claude-haiku-4-5", false}, 3}};
	hour.Requests = 4;
	summary.Activity.SubagentHours[Hour(2026, 11, 3, 9)] = hour;

	HourActivity silent;
	silent.Usage[{"claude-opus-5", false}] = {};
	silent.Usage[{"claude-unknown-9", false}] = {};
	silent.RequestsByModel = {{{"claude-opus-5", false}, 1}, {{"claude-unknown-9", false}, 2}};
	silent.Requests = 3;
	summary.Activity.MainHours[Hour(2026, 11, 3, 10)] = silent;

	const DashboardModel model = Build({&summary}, Day(2026, 11, 3));

	CHECK(model.Families[Family(ModelFamily::Opus)].Requests == 2);
	CHECK(model.Families[Family(ModelFamily::Haiku)].Requests == 3);
	CHECK(model.Families[Family(ModelFamily::Other)].Requests == 2);
	CHECK(model.UnpricedRequests == 2);
	CHECK(model.Requests == 7);
}

TEST_CASE("Projects fold worktrees into their repository and list the worktrees")
{
	SessionSummary main = Session("main", "D:\\Repos\\IDE");
	main.Activity.MainUsageByCwd["D:\\Repos\\IDE\\"] ={{{"claude-opus-5", false}, {1000, 0, 0, 0, 0}}};
	main.Activity.MainHours[Hour(2026, 11, 3, 9)] = OpusDollars(1);

	SessionSummary fox = Session("fox", "D:\\Repos\\IDE");
	fox.Activity.MainUsageByCwd["D:\\Repos\\IDE\\"] = {{{"claude-opus-5", false}, {10, 0, 0, 0, 0}}};
	fox.Activity.MainUsageByCwd["D:\\Repos\\IDE\\.claude\\worktrees\\fox\\docs"] = {
		{{"claude-opus-5", false}, {1000, 0, 0, 0, 0}}};
	fox.Activity.MainHours[Hour(2026, 11, 4, 9)] = OpusDollars(3);

	SessionSummary other = Session("other", "D:\\Other");
	other.Activity.MainUsageByCwd["D:\\Other"] = {{{"claude-opus-5", false}, {1000, 0, 0, 0, 0}}};
	other.Activity.MainHours[Hour(2026, 11, 4, 9)] = OpusDollars(2);

	const DashboardModel model = Build({&main, &fox, &other}, Day(2026, 11, 5));

	REQUIRE(model.Projects.size() == 2);
	const ProjectTotals& ide = model.Projects[0];
	CHECK(ide.Repository == std::filesystem::path{L"D:\\Repos\\IDE"});
	CHECK(ide.Sessions == 2);
	CHECK(ide.Worktrees == std::set<std::string>{"fox"});
	CHECK(ide.Cost == doctest::Approx(4.0));
	CHECK(ide.OpusCost == doctest::Approx(4.0));
	REQUIRE(ide.DailyCost.size() == model.Days.size());
	CHECK(ide.DailyCost[0] == doctest::Approx(1.0));
	CHECK(ide.DailyCost[1] == doctest::Approx(3.0));
	CHECK(ide.LastActive == Day(2026, 11, 4));
	CHECK(model.Sessions[ide.MostExpensiveSession].Summary == &fox);
	CHECK(model.Sessions[0].Place.Worktree == "fox");

	CHECK(model.Projects[1].Repository == std::filesystem::path{L"D:\\Other"});
	CHECK(model.Projects[1].Sessions == 1);
	CHECK(model.Projects[1].Worktrees.empty());
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("Worktree names differing only in case are one worktree")
{
	// Windows folders differ only in case without naming another folder.
	SessionSummary upper = Session("upper");
	upper.Activity.MainUsageByCwd["D:\\Repos\\IDE\\.claude\\worktrees\\Fox"] = {{{"claude-opus-5", false}, {1000, 0, 0, 0, 0}}};
	upper.Activity.MainHours[Hour(2026, 11, 3, 9)] = OpusDollars(2);

	SessionSummary lower = Session("lower");
	lower.Activity.MainUsageByCwd["D:\\Repos\\IDE\\.claude\\worktrees\\fox"] = {{{"claude-opus-5", false}, {1000, 0, 0, 0, 0}}};
	lower.Activity.MainHours[Hour(2026, 11, 3, 9)] = OpusDollars(1);

	const DashboardModel model = Build({&lower, &upper}, Day(2026, 11, 3));

	REQUIRE(model.Projects.size() == 1);
	CHECK(model.Projects[0].Worktrees == std::set<std::string>{"Fox"});
}
#endif

TEST_CASE("Tools leave out SubagentHandback and sort by calls; Sessions counts sessions using each")
{
	SessionSummary first = Session("a");
	first.Activity.Tools = {{"Read", {5, 1}}, {"Bash", {2, 0}}, {"SubagentHandback", {9, 0}}};
	first.Activity.Skills = {{"superpowers:tdd", {1, "2026-11-01T10:00:00.000Z"}}};

	SessionSummary second = Session("b");
	second.Activity.Tools = {{"Bash", {4, 2}}, {"Skill", {3, 0}}};
	second.Activity.Skills = {{"superpowers:tdd", {2, "2026-11-03T10:00:00.000Z"}}, {"grill-me", {1, "x"}}};

	const DashboardModel model = Build({&first, &second}, Day(2026, 11, 5));

	REQUIRE(model.Tools.size() == 3);
	CHECK(model.Tools[0].Name == "Bash");
	CHECK(model.Tools[0].Tally == ToolTally{6, 2});
	CHECK(model.Tools[0].Sessions == 2);
	CHECK(model.Tools[1].Name == "Read");
	CHECK(model.Tools[1].Sessions == 1);
	CHECK(model.Tools[2].Name == "Skill");
	CHECK(model.Tools[2].Tally.Calls == 3);

	REQUIRE(model.Skills.size() == 2);
	CHECK(model.Skills[0].Name == "superpowers:tdd");
	CHECK(model.Skills[0].Tally == SkillTally{3, "2026-11-03T10:00:00.000Z"});
	CHECK(model.Skills[0].Sessions == 2);
	CHECK(model.Skills[1].Name == "grill-me");
}

TEST_CASE("Unpriced requests are counted and cost nothing")
{
	SessionSummary summary = Session("a");
	summary.Activity.MainHours[Hour(2026, 11, 3, 9)] = Activity("claude-unknown-9", {1'000'000, 0, 0, 0, 0}, 3);
	summary.Activity.MainHours[Hour(2026, 11, 3, 10)] = OpusDollars(2, 2);
	summary.Activity.MainHours[Hour(2026, 11, 3, 11)] = Activity("<synthetic>", {}, 1);

	const DashboardModel model = Build({&summary}, Day(2026, 11, 3));

	CHECK(model.UnpricedRequests == 3);
	CHECK(model.Requests == 6);
	CHECK(model.Cost == doctest::Approx(2.0));
	CHECK(model.Families[Family(ModelFamily::Other)].Cost == 0.0);
	CHECK(model.Families[Family(ModelFamily::Other)].Requests == 4);
}

TEST_CASE("ThisWeek pairs each day since Monday with the same weekday a week before")
{
	SessionSummary summary = Session("a");
	summary.Activity.MainHours[Hour(2026, 11, 2, 9)] = OpusDollars(1);
	summary.Activity.MainHours[Hour(2026, 11, 3, 9)] = OpusDollars(2);
	summary.Activity.MainHours[Hour(2026, 11, 9, 9)] = OpusDollars(3);
	summary.Activity.MainHours[Hour(2026, 11, 10, 9)] = OpusDollars(4);

	const std::vector<WeekDay> week = ThisWeek(Build({&summary}, Day(2026, 11, 10)));

	REQUIRE(week.size() == 2);
	CHECK(week[0].Day == Day(2026, 11, 9));
	CHECK(week[0].Cost == doctest::Approx(3.0));
	CHECK(week[0].WeekBefore == doctest::Approx(1.0));
	CHECK(week[1].Day == Day(2026, 11, 10));
	CHECK(week[1].Cost == doctest::Approx(4.0));
	CHECK(week[1].WeekBefore == doctest::Approx(2.0));
}

TEST_CASE("Streak counts back from today; a quiet today starts from yesterday")
{
	SessionSummary summary = Session("a");
	summary.Activity.MainHours[Hour(2026, 11, 5, 9)] = OpusDollars(1);
	summary.Activity.MainHours[Hour(2026, 11, 7, 9)] = OpusDollars(1);
	summary.Activity.MainHours[Hour(2026, 11, 8, 9)] = OpusDollars(1);
	summary.Activity.MainHours[Hour(2026, 11, 9, 9)] = OpusDollars(1);

	CHECK(Streak(Build({&summary}, Day(2026, 11, 10))) == 3);
	CHECK(Streak(Build({&summary}, Day(2026, 11, 9))) == 3);
	CHECK(Streak(Build({&summary}, Day(2026, 11, 11))) == 0);

	summary.Activity.MainHours[Hour(2026, 11, 10, 9)] = OpusDollars(1);
	CHECK(Streak(Build({&summary}, Day(2026, 11, 10))) == 4);
}

TEST_CASE("HeatCuts and HeatLevel")
{
	SessionSummary summary = Session("a");
	for (unsigned day = 1; day <= 8; ++day)
	{
		summary.Activity.MainHours[Hour(2026, 11, day, 9)] = OpusDollars(day);
	}

	const DashboardModel model = Build({&summary}, Day(2026, 11, 10));
	const std::array<double, 3> cuts = HeatCuts(model);

	CHECK(cuts[0] == doctest::Approx(3.0));
	CHECK(cuts[1] == doctest::Approx(5.0));
	CHECK(cuts[2] == doctest::Approx(7.0));
	CHECK(HeatLevel(0.0, cuts, 8.0) == 0);
	CHECK(HeatLevel(3.0, cuts, 8.0) == 1);
	CHECK(HeatLevel(4.0, cuts, 8.0) == 2);
	CHECK(HeatLevel(6.0, cuts, 8.0) == 3);
	CHECK(HeatLevel(8.0, cuts, 8.0) == 4);
}

TEST_CASE("The costliest active day is the top heat level, however few days are active")
{
	for (const unsigned activeDays : {1U, 2U, 4U})
	{
		CAPTURE(activeDays);
		SessionSummary summary = Session("a");

		for (unsigned day = 1; day <= activeDays; ++day)
		{
			summary.Activity.MainHours[Hour(2026, 11, day, 9)] = OpusDollars(day);
		}

		const DashboardModel model = Build({&summary}, Day(2026, 11, 10));
		const auto highest = static_cast<double>(activeDays);

		CHECK(HeatLevel(highest, HeatCuts(model), highest) == 4);
		CHECK(HeatLevel(1.0, HeatCuts(model), highest) >= 1);
	}
}

TEST_CASE("ActiveDays and BusiestDay read only the days with cost")
{
	SessionSummary summary = Session("a");
	summary.Activity.MainHours[Hour(2026, 11, 1, 9)] = OpusDollars(4);
	summary.Activity.MainHours[Hour(2026, 11, 3, 9)] = OpusDollars(1);
	summary.Activity.MainHours[Hour(2026, 11, 4, 9)] = OpusDollars(7);

	const DashboardModel model = Build({&summary}, Day(2026, 11, 5));
	const ActiveDayStats stats = ActiveDays(model);

	CHECK(stats.Active == 3);
	CHECK(stats.Idle == 2);
	CHECK(stats.Average == doctest::Approx(4.0));
	CHECK(stats.Median == doctest::Approx(4.0));
	CHECK(stats.Cheapest == 2);
	CHECK(stats.Dearest == 3);
	CHECK(BusiestDay(model) == std::optional<std::size_t>{3});
	CHECK(Median({4.0, 1.0, 7.0, 2.0}) == doctest::Approx(3.0));
	CHECK(Median({}) == 0.0);
}

TEST_CASE("SessionsActiveOn orders by that day's cost")
{
	SessionSummary big = Session("big");
	big.Activity.MainHours[Hour(2026, 11, 3, 9)] = OpusDollars(1);
	big.Activity.MainHours[Hour(2026, 11, 4, 9)] = OpusDollars(10);

	SessionSummary small = Session("small");
	small.Activity.MainHours[Hour(2026, 11, 3, 9)] = OpusDollars(2);

	SessionSummary elsewhere = Session("elsewhere");
	elsewhere.Activity.MainHours[Hour(2026, 11, 4, 9)] = OpusDollars(5);

	const DashboardModel model = Build({&small, &elsewhere, &big}, Day(2026, 11, 5));

	REQUIRE(model.Sessions.size() == 3);
	CHECK(model.Sessions[0].Summary == &big);
	CHECK(model.Sessions[1].Summary == &elsewhere);
	CHECK(model.Sessions[2].Summary == &small);

	const std::vector<std::size_t> active = SessionsActiveOn(model, Day(2026, 11, 3));
	CHECK(active == std::vector<std::size_t>{2, 0});
	CHECK(SessionsActiveOn(model, Day(2026, 11, 5)).empty());
}

TEST_CASE("An empty or unpriced set builds a model without dividing by zero")
{
	const DashboardModel empty = Build({}, Day(2026, 11, 10));

	CHECK(empty.Days.empty());
	const ActiveDayStats stats = ActiveDays(empty);
	CHECK(stats.Average == 0.0);
	CHECK(stats.Median == 0.0);
	CHECK(stats.Cheapest == 0);
	CHECK(stats.Dearest == 0);
	CHECK(stats.Active == 0);
	CHECK(stats.Idle == 0);
	CHECK_FALSE(BusiestDay(empty).has_value());
	CHECK(HeatCuts(empty) == std::array<double, 3>{0.0, 0.0, 0.0});
	CHECK(Streak(empty) == 0);
	CHECK(SessionsActiveOn(empty, Day(2026, 11, 10)).empty());

	SessionSummary unpriced = Session("a");
	unpriced.Activity.MainHours[Hour(2026, 11, 9, 9)] = Activity("claude-unknown-9", {1000, 0, 0, 0, 0}, 1);
	const DashboardModel model = Build({&unpriced}, Day(2026, 11, 10));

	CHECK(model.Days.size() == 2);
	CHECK(ActiveDays(model).Active == 0);
	CHECK(ActiveDays(model).Idle == 2);
	CHECK(ActiveDays(model).Average == 0.0);
	CHECK_FALSE(BusiestDay(model).has_value());
	CHECK(HeatCuts(model) == std::array<double, 3>{0.0, 0.0, 0.0});
	CHECK(model.UnpricedRequests == 1);
}
