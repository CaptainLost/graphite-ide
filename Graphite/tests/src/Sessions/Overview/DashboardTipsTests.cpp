#include <doctest/doctest.h>

#include "Sessions/Overview/DashboardTips.h"
#include "Sessions/Overview/TipText.h"
#include "Sessions/SessionText.h"
#include "UI/TypographicMarks.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <vector>

using namespace Graphite;

namespace {
	constexpr std::uint64_t kOneDollarOfOpusInput = 200'000;
	constexpr std::uint64_t kOneDollarOfHaikuInput = 1'000'000;

	std::chrono::year_month_day Day(int year, unsigned month, unsigned day)
	{
		return std::chrono::year{year} / month / day;
	}

	UtcHour Hour(int year, unsigned month, unsigned day, int hour)
	{
		return std::chrono::sys_days{Day(year, month, day)} + std::chrono::hours{hour};
	}

	HourActivity Activity(const char* model, std::uint64_t input, std::size_t requests, std::size_t prompts = 0)
	{
		HourActivity activity;
		activity.Usage[UsageKey{model, false}] = {input, 0, 0, 0, 0};
		activity.Requests = requests;
		activity.RequestsByModel[UsageKey{model, false}] = requests;
		activity.Prompts = prompts;
		return activity;
	}

	HourActivity OpusDollars(double dollars, std::size_t prompts = 1)
	{
		return Activity("claude-opus-5", static_cast<std::uint64_t>(dollars * static_cast<double>(kOneDollarOfOpusInput)), 1,
						prompts);
	}

	std::string Minus(std::string_view amount)
	{
		return std::string{kMinusSign} + std::string{amount};
	}

	std::string Money(double dollars)
	{
		return FormatCost(CostResult{.Dollars = dollars});
	}

	// Active days: 2 Nov $1, 8 Nov $4 (big $3 with $1 of it a Haiku subagent, small $1), 9 Nov $1 plus one
	// request to a model without a price. Today is Monday 9 Nov 2026.
	struct Fixture
	{
		SessionSummary Big;
		SessionSummary Small;
		DashboardModel Model;

		Fixture()
		{
			Big.Id = "big";
			Big.Cwd = Core::FromUtf8("D:\\Repos\\IDE\\.claude\\worktrees\\fox");
			Big.Title = "Build the dashboard";
			Big.StartedAt = "2026-11-02T09:15:00.000Z";
			Big.EndedAt = "2026-11-08T20:40:00.000Z";
			Big.SubagentCount = 1;
			Big.Activity.ActiveTime = std::chrono::minutes{18 * 60 + 18};
			Big.Activity.Compactions = 2;
			Big.Activity.MainUsageByCwd["D:\\Repos\\IDE\\.claude\\worktrees\\fox"] = {
				{{"claude-opus-5", false}, {1000, 0, 0, 0, 0}}};
			Big.Activity.MainHours[Hour(2026, 11, 2, 10)] = OpusDollars(1);
			Big.Activity.MainHours[Hour(2026, 11, 8, 10)] = OpusDollars(2);
			Big.Activity.SubagentHours[Hour(2026, 11, 8, 11)] = Activity("claude-haiku-4-5", kOneDollarOfHaikuInput, 1);
			Big.Activity.Tools["PowerShell"] = {100, 12};
			Big.Activity.Tools["Bash"] = {100, 3};
			Big.Activity.Skills["superpowers:brainstorming"] = {4, "2026-11-08T23:30:00.000Z"};

			Small.Id = "small";
			Small.Cwd = Core::FromUtf8("D:\\Repos\\IDE");
			Small.Title = "Fix the list";
			Small.StartedAt = "2026-11-08T12:00:00.000Z";
			Small.EndedAt = "2026-11-09T08:00:00.000Z";
			Small.Activity.ActiveTime = std::chrono::minutes{45};
			Small.Activity.MainHours[Hour(2026, 11, 8, 12)] = OpusDollars(1);
			HourActivity mixed = OpusDollars(1);
			mixed.Usage[UsageKey{"claude-unknown-9", false}] = {500, 0, 0, 0, 0};
			mixed.RequestsByModel[UsageKey{"claude-unknown-9", false}] = 1;
			mixed.Requests = 2;
			Small.Activity.MainHours[Hour(2026, 11, 9, 7)] = mixed;
			Small.Activity.Tools["Bash"] = {5, 0};

			const std::vector<const SessionSummary*> sessions{&Big, &Small};
			Model = BuildDashboard(sessions, Pricing::Defaults(), LocalCalendar{nullptr}, Day(2026, 11, 9));
		}

		std::size_t DayAt(unsigned day) const
		{
			return *DayIndex(Model, Day(2026, 11, day));
		}

		std::size_t ToolNamed(std::string_view name) const
		{
			const auto found = std::find_if(Model.Tools.begin(), Model.Tools.end(),
											[&](const NamedTool& tool) { return tool.Name == name; });
			REQUIRE(found != Model.Tools.end());
			return static_cast<std::size_t>(found - Model.Tools.begin());
		}
	};

	const TipSection* SectionTitled(const TipCard& card, std::string_view title)
	{
		for (const TipSection& section : card.Sections)
		{
			if (section.Title == title)
			{
				return &section;
			}
		}

		return nullptr;
	}

	const TipRow* RowLabelled(const TipCard& card, std::string_view label)
	{
		for (const TipSection& section : card.Sections)
		{
			for (const TipRow& row : section.Rows)
			{
				if (row.Label == label)
				{
					return &row;
				}
			}
		}

		return nullptr;
	}

	std::vector<std::string> AllNotes(const TipCard& card)
	{
		std::vector<std::string> notes = card.Notes;

		for (const TipSection& section : card.Sections)
		{
			notes.insert(notes.end(), section.Notes.begin(), section.Notes.end());
		}

		return notes;
	}

	bool HasNote(const TipCard& card, std::string_view note)
	{
		const std::vector<std::string> notes = AllNotes(card);
		return std::find(notes.begin(), notes.end(), note) != notes.end();
	}
}

TEST_CASE("The Spent tip breaks the cost down by model, thread and project and names unpriced requests")
{
	const Fixture fixture;
	const TipCard card = NumberTip(fixture.Model, NumberTile::Spent, std::nullopt);

	CHECK(card.Head == "Spent in 8 days");
	CHECK(card.HeadValue == FormatCost(CostResult{.Dollars = fixture.Model.Cost}));
	CHECK(card.HeadValue == "$6.00");
	CHECK(card.Banner.empty());

	const TipSection* byModel = SectionTitled(card, "By model");
	REQUIRE(byModel != nullptr);
	REQUIRE(byModel->Rows.size() == 3);
	CHECK(byModel->Rows[0].Label == "Opus");
	CHECK(byModel->Rows[0].Swatch == ModelFamily::Opus);
	CHECK(byModel->Rows[0].Value == "$5.00");
	CHECK(byModel->Rows[0].Extra == "83%");
	CHECK(byModel->Rows[1].Label == "Haiku");
	CHECK(byModel->Rows[2].Label == "unknown-9");
	CHECK(byModel->Rows[2].Swatch == ModelFamily::Other);
	CHECK(byModel->Rows[2].Value == "?");
	CHECK(byModel->Rows[2].Extra.empty());
	CHECK(byModel->Notes == std::vector<std::string>{"No price for claude-unknown-9. Add a price under Settings > Claude."});

	const TipSection* byThread = SectionTitled(card, "By thread");
	REQUIRE(byThread != nullptr);
	REQUIRE(byThread->Rows.size() == 2);
	CHECK(byThread->Rows[0].Label == "Main thread");
	CHECK(byThread->Rows[0].Value == "$5.00");
	CHECK(byThread->Rows[1].Label == "Subagents");
	CHECK(byThread->Rows[1].Value == "$1.00");
	CHECK(byThread->Rows[1].Extra == "17%");

	const TipSection* projects = SectionTitled(card, "Top projects");
	REQUIRE(projects != nullptr);
	REQUIRE(projects->Rows.size() == 1);
	CHECK(projects->Rows[0].Label == "IDE");
	CHECK(projects->Rows[0].Extra == "100%");

	CHECK(HasNote(card, "1 request went to a model with no price and counts as $0."));
	CHECK(card.Sections.back().Notes.size() == 1);
}

TEST_CASE("A model without a price is listed with a question mark wherever its family is, and named in the tip")
{
	const Fixture fixture;
	const std::string hint = "No price for claude-unknown-9. Add a price under Settings > Claude.";

	const TipCard day = DayTip(fixture.Model, fixture.DayAt(9));
	REQUIRE(RowLabelled(day, "unknown-9") != nullptr);
	CHECK(RowLabelled(day, "unknown-9")->Value == "?");
	CHECK(HasNote(day, hint));
	CHECK(RowLabelled(DayTip(fixture.Model, fixture.DayAt(8)), "unknown-9") == nullptr);
	CHECK_FALSE(HasNote(DayTip(fixture.Model, fixture.DayAt(8)), hint));

	REQUIRE(fixture.Model.Sessions[1].Summary == &fixture.Small);
	CHECK(fixture.Model.Sessions[1].UnpricedRequests == 1);
	const TipCard session = SessionTip(fixture.Model, 1, LocalCalendar{nullptr});
	CHECK(session.HeadValue == "$2.00 ?");
	CHECK(HasNote(session, hint));
	REQUIRE(RowLabelled(session, "unknown-9") != nullptr);
	CHECK(RowLabelled(session, "unknown-9")->Value == "?");
	CHECK(SessionTip(fixture.Model, 0, LocalCalendar{nullptr}).HeadValue == "$4.00");

	const TipCard other = ModelTip(fixture.Model, ModelFamily::Other);
	CHECK(other.Head == "unknown-9");
	CHECK(other.HeadValue == "?");
	CHECK(RowLabelled(other, "Requests")->Value == "1");
	CHECK(RowLabelled(other, "Per request")->Value == "?");
	CHECK(HasNote(other, hint));

	CHECK(RowLabelled(NumberTip(fixture.Model, NumberTile::Output, std::nullopt), "unknown-9")->Value == "0");
}

TEST_CASE("Several models without a price stay Other, and its tip names each")
{
	SessionSummary summary;
	summary.Id = "a";
	summary.Cwd = Core::FromUtf8("D:\\Repos\\IDE");
	summary.Activity.MainHours[Hour(2026, 11, 9, 9)] = Activity("claude-unknown-9", 1000, 2);
	summary.Activity.MainHours[Hour(2026, 11, 9, 10)] = Activity("claude-mystery-1", 1000, 1);
	summary.Activity.MainHours[Hour(2026, 11, 9, 11)] = Activity("claude-mythos-5", 1000, 1);

	const std::vector<const SessionSummary*> sessions{&summary};
	const DashboardModel model = BuildDashboard(sessions, Pricing::Defaults(), LocalCalendar{nullptr}, Day(2026, 11, 9));

	const TipCard spent = NumberTip(model, NumberTile::Spent, std::nullopt);
	REQUIRE(RowLabelled(spent, "Other") != nullptr);
	CHECK(RowLabelled(spent, "Other")->Value == "?");
	CHECK(HasNote(spent, "No price for claude-mystery-1, claude-unknown-9. Add a price under Settings > Claude."));

	const TipCard other = ModelTip(model, ModelFamily::Other);
	CHECK(other.Head == "Other");
	const TipSection* models = SectionTitled(other, "Models");
	REQUIRE(models != nullptr);
	REQUIRE(models->Rows.size() == 3);
	CHECK(models->Rows[0].Label == "mystery-1");
	CHECK(models->Rows[0].Extra == "no price");
	CHECK(models->Rows[1].Label == "mythos-5");
	CHECK(models->Rows[1].Extra.empty());
	CHECK(models->Rows[2].Label == "unknown-9");
	CHECK(models->Rows[2].Value == "2 requests");
}

TEST_CASE("One priced model of its own is still Other")
{
	SessionSummary summary;
	summary.Id = "a";
	summary.Cwd = Core::FromUtf8("D:\\Repos\\IDE");
	summary.Activity.MainHours[Hour(2026, 11, 9, 11)] = Activity("claude-mythos-5", 100'000, 1);

	const std::vector<const SessionSummary*> sessions{&summary};
	const DashboardModel model = BuildDashboard(sessions, Pricing::Defaults(), LocalCalendar{nullptr}, Day(2026, 11, 9));

	CHECK(FamilyLabel(model, ModelFamily::Other) == "Other");
	CHECK(ModelTip(model, ModelFamily::Other).HeadValue == "$1.00");
}

TEST_CASE("A number tip over a sparkline day carries that day's value as a banner")
{
	const Fixture fixture;
	const std::size_t today = fixture.DayAt(9);

	CHECK(NumberTip(fixture.Model, NumberTile::Spent, today).Banner == "Mon 9 Nov: " + Money(1));
	CHECK(NumberTip(fixture.Model, NumberTile::PerActiveDay, fixture.DayAt(5)).Banner == "Thu 5 Nov: no activity");
	CHECK(NumberTip(fixture.Model, NumberTile::Sessions, fixture.DayAt(8)).Banner == "Sun 8 Nov: 1 started");
	CHECK(NumberTip(fixture.Model, NumberTile::Subagents, fixture.DayAt(8)).Banner == "Sun 8 Nov: 25% of the day");
	CHECK(NumberTip(fixture.Model, NumberTile::Spent, std::size_t{99}).Banner.empty());
}

TEST_CASE("The Busiest day tip is that day's tip")
{
	const Fixture fixture;
	const TipCard card = NumberTip(fixture.Model, NumberTile::BusiestDay, std::nullopt);

	CHECK(card.Head == "Sun 8 Nov 2026");
	CHECK(card.HeadValue == "$4.00");
}

TEST_CASE("The other number tiles say what they add up")
{
	const Fixture fixture;

	const TipCard week = NumberTip(fixture.Model, NumberTile::ThisWeek, std::nullopt);
	CHECK(week.Head == "This week, day by day");
	REQUIRE(week.Sections.size() == 1);
	REQUIRE(week.Sections[0].Rows.size() == 1);
	CHECK(week.Sections[0].Rows[0].Label == "Mon 9");
	CHECK(week.Sections[0].Rows[0].Extra == Money(1) + " a week before");

	const TipCard perDay = NumberTip(fixture.Model, NumberTile::PerActiveDay, std::nullopt);
	REQUIRE(RowLabelled(perDay, "Average") != nullptr);
	CHECK(RowLabelled(perDay, "Average")->Value == "$2.00");
	CHECK(RowLabelled(perDay, "Most expensive")->Extra == "8 Nov");
	CHECK(RowLabelled(perDay, "Days without Claude")->Value == "5");

	const TipCard sessions = NumberTip(fixture.Model, NumberTile::Sessions, std::nullopt);
	CHECK(sessions.Head == "2 sessions");
	CHECK(RowLabelled(sessions, "Compactions")->Value == "2");
	CHECK(RowLabelled(sessions, "Build the dashboard")->Value == "18h 18m");

	const TipCard subagents = NumberTip(fixture.Model, NumberTile::Subagents, std::nullopt);
	CHECK(subagents.HeadValue == "$1.00");
	CHECK(RowLabelled(subagents, "Runs")->Value == "1");
	REQUIRE(SectionTitled(subagents, "Sessions that used them most") != nullptr);
	CHECK(SectionTitled(subagents, "Sessions that used them most")->Rows.size() == 1);
	CHECK(RowLabelled(subagents, "Build the dashboard")->Extra == "25% of it");
}

TEST_CASE("A day tip sets the day against an average active day and lists the sessions active on it")
{
	const Fixture fixture;
	const TipCard card = DayTip(fixture.Model, fixture.DayAt(8));

	CHECK(card.Head == "Sun 8 Nov 2026");
	CHECK(card.HeadValue == "$4.00");
	CHECK(card.Banner.empty());
	REQUIRE(card.Notes.size() == 1);
	CHECK(card.Notes[0] == "2.0\xC3\x97 an average active day ($2.00)");

	CHECK(RowLabelled(card, "Subagents")->Value == "$1.00");
	CHECK(RowLabelled(card, "Requests")->Value == "3");

	const TipSection* active = SectionTitled(card, "Sessions active that day \xC2\xB7 2");
	REQUIRE(active != nullptr);
	REQUIRE(active->Rows.size() == 2);
	CHECK(active->Rows[0].Label == "Build the dashboard");
	CHECK(active->Rows[0].Value == "$3.00");
	CHECK(active->Rows[0].Extra == "IDE \xC2\xB7 fox");
	CHECK(active->Rows[1].Label == "Fix the list");
	CHECK(active->Notes == std::vector<std::string>{"Click to list them"});

	CHECK(DayTip(fixture.Model, fixture.DayAt(2)).Notes[0] == "50% of an average active day ($2.00)");
	CHECK(SectionTitled(DayTip(fixture.Model, fixture.DayAt(5)), "Sessions active that day \xC2\xB7 0") == nullptr);
}

TEST_CASE("A day tip lists five sessions and counts the rest")
{
	std::vector<SessionSummary> summaries(7);
	std::vector<const SessionSummary*> sessions;

	for (std::size_t index = 0; index < summaries.size(); ++index)
	{
		summaries[index].Id = std::to_string(index);
		summaries[index].Cwd = Core::FromUtf8("D:\\Repos\\IDE");
		summaries[index].Activity.MainHours[Hour(2026, 11, 9, 10)] = OpusDollars(static_cast<double>(index + 1));
		sessions.push_back(&summaries[index]);
	}

	const DashboardModel model = BuildDashboard(sessions, Pricing::Defaults(), LocalCalendar{nullptr}, Day(2026, 11, 9));
	const TipCard card = DayTip(model, 0);

	const TipSection* active = SectionTitled(card, "Sessions active that day \xC2\xB7 7");
	REQUIRE(active != nullptr);
	CHECK(active->Rows.size() == 5);
	CHECK(active->Rows[0].Value == "$7.00");
	CHECK(active->Notes == std::vector<std::string>{"and 2 more \xC2\xB7 click to list them"});
}

TEST_CASE("A gone day says the transcripts were deleted and Graphite keeps no copy")
{
	const TipCard card = GoneDayTip(Day(2026, 10, 11));

	CHECK(card.Head == "Sun 11 Oct 2026");
	CHECK(card.HeadValue.empty());
	REQUIRE(card.Notes.size() == 1);
	CHECK(card.Notes[0].find("cleanupPeriodDays") != std::string::npos);
	CHECK(card.Notes[0].find("Graphite keeps no copy") != std::string::npos);
}

TEST_CASE("A session tip spans its days and counts its failed tool calls")
{
	const Fixture fixture;
	REQUIRE(fixture.Model.Sessions[0].Summary == &fixture.Big);
	const TipCard card = SessionTip(fixture.Model, 0, LocalCalendar{nullptr});

	CHECK(card.Head == "IDE \xC2\xB7 fox");
	CHECK(card.HeadValue == "$4.00");
	CHECK(card.Body == "Build the dashboard");

	CHECK(RowLabelled(card, "Started")->Value == "Mon 2 Nov, 09:15");
	CHECK(RowLabelled(card, "Last record")->Value == "Sun 8 Nov, 20:40");
	const TipRow* active = RowLabelled(card, "Active time");
	REQUIRE(active != nullptr);
	CHECK(active->Value == "18h 18m");
	CHECK(active->Extra == "over 2 Nov\xE2\x80\x93" "8 Nov");

	CHECK(RowLabelled(card, "Prompts you sent")->Extra == "$2.00 each");
	CHECK(RowLabelled(card, "Subagent runs")->Extra == "$1.00");

	const TipSection* tools = SectionTitled(card, "Tools");
	REQUIRE(tools != nullptr);
	CHECK(tools->Rows.size() == 2);
	CHECK(tools->Notes == std::vector<std::string>{"15 tool calls failed", "Click to open its tab"});
	CHECK(&card.Sections.back() == tools);
}

TEST_CASE("A session within a day has no span and no failed note")
{
	const Fixture fixture;
	const TipCard card = SessionTip(fixture.Model, 1, LocalCalendar{nullptr});

	CHECK(card.Head == "IDE");
	CHECK(RowLabelled(card, "Active time")->Value == "45m");
	CHECK(RowLabelled(card, "Active time")->Extra.empty());
	CHECK_FALSE(HasNote(card, "0 tool calls failed"));
	CHECK(HasNote(card, "Click to open its tab"));
}

TEST_CASE("A tool's failure rate turns bad above five percent")
{
	const Fixture fixture;

	const TipCard powerShell = ToolTip(fixture.Model, fixture.ToolNamed("PowerShell"));
	CHECK(powerShell.Head == "PowerShell");
	CHECK(powerShell.HeadValue == "100 calls");
	const TipRow* failed = RowLabelled(powerShell, "Failed");
	REQUIRE(failed != nullptr);
	CHECK(failed->Value == "12");
	CHECK(failed->Extra == "12%");
	CHECK(failed->ExtraTone == TipTone::Bad);
	CHECK(RowLabelled(powerShell, "Sessions using it")->Extra == "of 2");

	const TipCard bash = ToolTip(fixture.Model, fixture.ToolNamed("Bash"));
	CHECK(RowLabelled(bash, "Failed")->Extra == "3%");
	CHECK(RowLabelled(bash, "Failed")->ExtraTone == TipTone::Dim);
	CHECK(RowLabelled(bash, "Sessions using it")->Value == "2");
	CHECK(RowLabelled(bash, "Per session that used it")->Value == "53");
}

TEST_CASE("Model, project, skill, punch and cache tips")
{
	const Fixture fixture;

	const TipCard haiku = ModelTip(fixture.Model, ModelFamily::Haiku);
	CHECK(haiku.Head == "Haiku");
	CHECK(haiku.HeadValue == "$1.00");
	CHECK(RowLabelled(haiku, "Requests")->Value == "1");
	CHECK(RowLabelled(haiku, "Spent by subagents")->Extra == "100%");
	CHECK(RowLabelled(haiku, "Uncached input")->Value == "1.0M");

	const TipCard project = ProjectTip(fixture.Model, 0);
	CHECK(project.Head == "IDE");
	CHECK(project.Body == "D:\\Repos\\IDE");
	CHECK(RowLabelled(project, "Active days")->Value == "3");
	CHECK(RowLabelled(project, "Last activity")->Value == "today");
	CHECK(RowLabelled(project, "Busiest day")->Extra == "8 Nov");
	REQUIRE(SectionTitled(project, "Worktrees \xC2\xB7 1") != nullptr);
	CHECK(SectionTitled(project, "Worktrees \xC2\xB7 1")->Rows[0].Label == "fox");
	CHECK(SectionTitled(project, "Worktrees \xC2\xB7 1")->Rows[0].CodeLabel);
	CHECK(RowLabelled(project, "Build the dashboard")->Value == "$4.00");

	const TipCard skill = SkillTip(fixture.Model, 0, LocalCalendar{nullptr});
	CHECK(skill.Head == "superpowers:brainstorming");
	CHECK(skill.HeadValue == "4 calls");
	CHECK(RowLabelled(skill, "Last used")->Value == "Sun 8 Nov, 23:30");

	const TipCard punch = PunchTip(fixture.Model, 6, 10);
	CHECK(punch.Head == "Sun 10:00\xE2\x80\x93" "11:00");
	CHECK(punch.HeadValue == "$2.00");
	CHECK(RowLabelled(punch, "Share of the cost")->Value == "33%");

	const TipCard read = CacheTip(fixture.Model, CachePart::Uncached);
	CHECK(read.Head == "Uncached");
	REQUIRE(read.Notes.size() == 1);
	CHECK(read.Notes[0] == "Input charged at the full price: only the newest part of each request.");

	const TipCard written = CacheTip(fixture.Model, CachePart::Written);
	CHECK(written.Head == "Written to cache");
	REQUIRE(written.Notes.size() == 1);
	CHECK(written.Notes[0].starts_with("Tokens put into the cache for the next request; charged at 1.25\xC3\x97 (5 minutes) or "
									   "2\xC3\x97 (1 hour)"));
}

TEST_CASE("A loss and a tiny share read as a person writes them")
{
	CHECK(TipMoney(-0.5) == Minus("$0.50"));
	CHECK(TipMoney(0.5) == "$0.50");
	CHECK(TipShare(-12.0, 100.0) == Minus("12%"));
	CHECK(TipShare(0.0005, 1.0) == "<0.1%");
	CHECK(TipShare(0.004, 1.0) == "0.4%");
	CHECK(TipShare(0.0, 1.0) == "0%");
	CHECK(TipMoney(-0.001) == "$0.00");
	CHECK(CompactMoney(-0.001) == "$0.00");
	CHECK(TipMoney(-0.006) == Minus("$0.01"));
}

TEST_CASE("A share in tenths reads <0.1% below a tenth, never 0.0%")
{
	CHECK(TipShareInTenths(985.0, 1000.0) == "98.5%");
	CHECK(TipShareInTenths(785.0, 14'600'000.0) == "<0.1%");
	CHECK(TipShareInTenths(0.0, 1000.0) == "0.0%");
	CHECK(TipShareInTenths(1.0, 0.0) == std::string{kEnDash});
}

TEST_CASE("Compact money writes a thousand and more in k and M, rounded before the unit is chosen")
{
	CHECK(CompactMoney(372.91) == "$372.91");
	CHECK(CompactMoney(999.99) == "$999.99");
	CHECK(CompactMoney(999.996) == "$1.0k");
	CHECK(CompactMoney(1000.0) == "$1.0k");
	CHECK(CompactMoney(4476.59) == "$4.5k");
	CHECK(CompactMoney(9999.0) == "$10.0k");
	CHECK(CompactMoney(50'612.0) == "$50.6k");
	CHECK(CompactMoney(999'949.0) == "$999.9k");
	CHECK(CompactMoney(999'950.0) == "$1.0M");
	CHECK(CompactMoney(1e6) == "$1.0M");
	CHECK(CompactMoney(0.0) == "$0.00");
	CHECK(CompactMoney(-4476.59) == Minus("$4.5k"));
}

TEST_CASE("A day without cost is not set against an average active day")
{
	const Fixture fixture;

	CHECK(DayTip(fixture.Model, fixture.DayAt(5)).Notes.empty());
	CHECK(DayTip(fixture.Model, fixture.DayAt(8)).Notes.size() == 1);
}

TEST_CASE("Saved by model keeps a family that lost on the cache, so its rows add up to the head")
{
	SessionSummary summary;
	summary.Id = "a";
	summary.Cwd = Core::FromUtf8("D:/Repos/IDE");
	HourActivity read = Activity("claude-opus-5", 0, 1);
	read.Usage[UsageKey{"claude-opus-5", false}] = {0, 0, 0, 0, 1'000'000};
	HourActivity written = Activity("claude-haiku-4-5", 0, 1);
	written.Usage[UsageKey{"claude-haiku-4-5", false}] = {0, 0, 0, 1'000'000, 0};
	summary.Activity.MainHours[Hour(2026, 11, 9, 9)] = read;
	summary.Activity.MainHours[Hour(2026, 11, 9, 10)] = written;

	const std::vector<const SessionSummary*> sessions{&summary};
	const DashboardModel model = BuildDashboard(sessions, Pricing::Defaults(), LocalCalendar{nullptr}, Day(2026, 11, 9));
	const TipCard card = NumberTip(model, NumberTile::CacheSaved, std::nullopt);

	CHECK(card.HeadValue == "$3.50");
	const TipSection* byModel = SectionTitled(card, "Saved by model");
	REQUIRE(byModel != nullptr);
	REQUIRE(byModel->Rows.size() == 2);
	CHECK(byModel->Rows[0].Value == "$4.50");
	CHECK(byModel->Rows[1].Label == "Haiku");
	CHECK(byModel->Rows[1].Value == Minus("$1.00"));
}

TEST_CASE("The Subagents banner rounds its share as the tips do")
{
	SessionSummary summary;
	summary.Id = "a";
	summary.Cwd = Core::FromUtf8("D:/Repos/IDE");
	summary.Activity.MainHours[Hour(2026, 11, 9, 10)] = OpusDollars(4000);
	summary.Activity.SubagentHours[Hour(2026, 11, 9, 10)] = Activity("claude-haiku-4-5", kOneDollarOfHaikuInput, 1);

	const std::vector<const SessionSummary*> sessions{&summary};
	const DashboardModel model = BuildDashboard(sessions, Pricing::Defaults(), LocalCalendar{nullptr}, Day(2026, 11, 9));

	CHECK(NumberTip(model, NumberTile::Subagents, std::size_t{0}).Banner == "Mon 9 Nov: <0.1% of the day");
}

TEST_CASE("A share under one percent keeps a decimal")
{
	SessionSummary summary;
	summary.Id = "a";
	summary.Cwd = Core::FromUtf8("D:\\Repos\\IDE");
	summary.Activity.MainHours[Hour(2026, 11, 9, 10)] = OpusDollars(400);
	summary.Activity.SubagentHours[Hour(2026, 11, 9, 10)] = Activity("claude-haiku-4-5", kOneDollarOfHaikuInput, 1);

	const std::vector<const SessionSummary*> sessions{&summary};
	const DashboardModel model = BuildDashboard(sessions, Pricing::Defaults(), LocalCalendar{nullptr}, Day(2026, 11, 9));

	CHECK(RowLabelled(NumberTip(model, NumberTile::Spent, std::nullopt), "Subagents")->Extra == "0.2%");
}
