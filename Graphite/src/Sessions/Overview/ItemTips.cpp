#include "Sessions/Overview/DashboardTips.h"

#include "Sessions/Overview/TipText.h"
#include "Sessions/SessionText.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "UI/TypographicMarks.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <format>
#include <numeric>
#include <utility>

namespace Graphite {

	namespace {
		using Date = std::chrono::year_month_day;

		constexpr std::size_t kListedWorktrees = 5;
		constexpr std::size_t kListedTools = 5;
		constexpr std::size_t kBodyBytes = 2000;
		constexpr double kBadFailureRate = 0.05;
		constexpr std::chrono::hours kOneDay{24};
		constexpr std::string_view kSubagentHandback = "SubagentHandback";

		std::string Moment(std::string_view isoTimestamp, const LocalCalendar& calendar)
		{
			const std::optional<std::chrono::local_time<std::chrono::minutes>> minute = calendar.MinuteOf(isoTimestamp);

			if (!minute)
			{
				return std::string{kEnDash};
			}

			const auto days = std::chrono::floor<std::chrono::days>(*minute);
			const std::chrono::hh_mm_ss<std::chrono::minutes> time{*minute - days};
			return std::format("{}, {:02}:{:02}", WeekdayDate(Date{days}), time.hours().count(), time.minutes().count());
		}

		std::string SpanOf(const SessionSummary& summary, const LocalCalendar& calendar)
		{
			const auto started = ParseTimestamp(summary.StartedAt);
			const auto ended = ParseTimestamp(summary.EndedAt);

			if (!started || !ended || *ended - *started <= kOneDay)
			{
				return {};
			}

			const std::optional<Date> first = calendar.DayOf(summary.StartedAt);
			const std::optional<Date> last = calendar.DayOf(summary.EndedAt);

			if (!first || !last)
			{
				return {};
			}

			return std::format("over {}{}{}", ShortDate(*first), kEnDash, ShortDate(*last));
		}

		TipSection ToolsOf(const SessionSummary& summary)
		{
			std::vector<std::pair<std::string, ToolTally>> tools;
			std::size_t failed = 0;

			for (const auto& [name, tally] : summary.Activity.Tools)
			{
				if (name != kSubagentHandback && tally.Calls > 0)
				{
					tools.emplace_back(name, tally);
					failed += tally.Failed;
				}
			}

			std::stable_sort(tools.begin(), tools.end(),
							 [](const auto& left, const auto& right) { return left.second.Calls > right.second.Calls; });

			TipSection section{.Title = "Tools"};
			for (std::size_t index = 0; index < std::min(kListedTools, tools.size()); ++index)
			{
				section.Rows.push_back({tools[index].first, TipCount(tools[index].second.Calls)});
			}

			if (failed > 0)
			{
				section.Notes.push_back(failed == 1 ? std::string{"1 tool call failed"}
													: std::format("{} tool calls failed", TipCount(failed)));
			}

			return section;
		}

		std::string LastActivity(const DashboardModel& model, const ProjectTotals& project)
		{
			if (!project.LastActive)
			{
				return std::string{kEnDash};
			}

			const auto daysAgo = (std::chrono::sys_days{model.Today} - std::chrono::sys_days{*project.LastActive}).count();
			return daysAgo <= 0 ? std::string{"today"} : std::format("{}d ago", daysAgo);
		}

		TipSection WorktreesOf(const ProjectTotals& project)
		{
			TipSection section{.Title = std::format("Worktrees{}{}", kMiddleDotSeparator, project.Worktrees.size())};

			for (const std::string& name : project.Worktrees)
			{
				if (section.Rows.size() == kListedWorktrees)
				{
					section.Notes.push_back(std::format("and {} more", project.Worktrees.size() - kListedWorktrees));
					break;
				}

				section.Rows.push_back({.Label = name, .CodeLabel = true});
			}

			return section;
		}
	}

	TipCard ModelTip(const DashboardModel& model, ModelFamily family)
	{
		const std::size_t index = std::min(static_cast<std::size_t>(family), kModelFamilyCount - 1);
		const FamilyTotals& totals = model.Families[index];
		const bool unpriced = totals.UnpricedRequests > 0;
		const std::string unknown{kUnknownAmount};
		TipCard card{.Head = FamilyLabel(model, family), .HeadValue = unpriced ? unknown : TipMoney(totals.Cost)};
		NoteUnpriced(card.Notes, totals.UnpricedModels);

		TipSection costs;
		costs.Rows.push_back({"Requests", TipCount(totals.Requests)});
		costs.Rows.push_back({"Per request", totals.Requests == 0 ? std::string{kEnDash}
											 : unpriced			  ? unknown
																  : TipMoney(totals.Cost / static_cast<double>(totals.Requests))});
		costs.Rows.push_back({"Share of the cost", unpriced ? unknown : TipShare(totals.Cost, model.Cost)});
		const bool subagentsUnpriced = totals.UnpricedSubagentRequests > 0;
		costs.Rows.push_back({"Spent by subagents", subagentsUnpriced ? unknown : TipMoney(totals.SubagentCost),
							  subagentsUnpriced ? std::string{} : TipShare(totals.SubagentCost, totals.Cost)});
		card.Sections.push_back(std::move(costs));

		if (totals.RequestsByModel.size() > 1 && family == ModelFamily::Other)
		{
			TipSection models{.Title = "Models"};

			for (const auto& [name, requests] : totals.RequestsByModel)
			{
				models.Rows.push_back({ModelLabel(name, false), TipCounted(requests, "request", "requests"),
									   totals.UnpricedModels.contains(name) ? std::string{"no price"} : std::string{}});
			}

			card.Sections.push_back(std::move(models));
		}

		TipSection tokens{.Title = "Tokens"};
		tokens.Rows.push_back({"Output", FormatTokens(totals.Tokens.Output)});
		tokens.Rows.push_back({"Read from cache", FormatTokens(totals.Tokens.CacheRead)});
		tokens.Rows.push_back({"Written to cache", FormatTokens(totals.Tokens.CacheWrite)});
		tokens.Rows.push_back({"Uncached input", FormatTokens(totals.Tokens.Input)});
		card.Sections.push_back(std::move(tokens));

		TipSection saved;
		saved.Rows.push_back({"Cache saved", unpriced ? unknown : TipMoney(totals.Saved)});
		card.Sections.push_back(std::move(saved));
		return card;
	}

	TipCard ProjectTip(const DashboardModel& model, std::size_t projectIndex)
	{
		if (projectIndex >= model.Projects.size())
		{
			return {};
		}

		const ProjectTotals& project = model.Projects[projectIndex];
		TipCard card{.Head = ProjectName(project.Repository), .HeadValue = TipMoney(project.Cost),
					 .Body = Core::ToUtf8(project.Repository)};

		const auto activeDays = static_cast<std::size_t>(
			std::count_if(project.DailyCost.begin(), project.DailyCost.end(), [](double cost) { return cost > 0.0; }));

		TipSection facts;
		facts.Rows.push_back({"Sessions", TipCount(project.Sessions)});
		facts.Rows.push_back({"Active days", TipCount(activeDays)});
		facts.Rows.push_back({"Last activity", LastActivity(model, project)});

		const auto busiest = std::max_element(project.DailyCost.begin(), project.DailyCost.end());
		if (busiest != project.DailyCost.end() && *busiest > 0.0)
		{
			const auto dayIndex = static_cast<std::size_t>(busiest - project.DailyCost.begin());
			facts.Rows.push_back({"Busiest day", TipMoney(*busiest), ShortDate(model.Days[dayIndex].Day)});
		}

		facts.Rows.push_back({"Opus share", TipShare(project.OpusCost, project.Cost)});
		facts.Rows.push_back({"Subagents", TipMoney(project.SubagentCost), TipShare(project.SubagentCost, project.Cost)});
		card.Sections.push_back(std::move(facts));

		if (!project.Worktrees.empty())
		{
			card.Sections.push_back(WorktreesOf(project));
		}

		if (project.MostExpensiveSession < model.Sessions.size())
		{
			const SessionFacts& session = model.Sessions[project.MostExpensiveSession];
			TipSection most{.Title = "Most expensive session"};
			most.Rows.push_back({SessionTitle(session), TipMoney(session.Cost)});
			card.Sections.push_back(std::move(most));
		}

		return card;
	}

	TipCard SessionTip(const DashboardModel& model, std::size_t sessionIndex, const LocalCalendar& calendar)
	{
		if (sessionIndex >= model.Sessions.size() || model.Sessions[sessionIndex].Summary == nullptr)
		{
			return {};
		}

		const SessionFacts& session = model.Sessions[sessionIndex];
		const SessionSummary& summary = *session.Summary;
		TipCard card{.Head = PlaceLabel(session.Place), .HeadValue = SessionMoney(session),
					 .Body = SessionTitle(session, kBodyBytes)};
		NoteUnpriced(card.Notes, session.UnpricedModels);

		TipSection times;
		times.Rows.push_back({"Started", Moment(summary.StartedAt, calendar)});
		times.Rows.push_back({"Last record", Moment(summary.EndedAt, calendar)});
		times.Rows.push_back({"Active time", FormatDuration(summary.Activity.ActiveTime), SpanOf(summary, calendar)});
		card.Sections.push_back(std::move(times));

		TipSection counts;
		counts.Rows.push_back({"Prompts you sent", TipCount(session.Prompts),
							   session.Prompts == 0 ? std::string{}
													: TipMoney(session.Cost / static_cast<double>(session.Prompts)) + " each"});
		counts.Rows.push_back({"Requests", TipCount(session.Requests)});
		counts.Rows.push_back({"Subagent runs", TipCount(summary.SubagentCount),
							   session.SubagentCost > 0.0 ? TipMoney(session.SubagentCost) : std::string{}});
		counts.Rows.push_back({"Compactions", TipCount(summary.Activity.Compactions)});
		counts.Rows.push_back({"Output tokens", FormatTokens(session.Output)});
		card.Sections.push_back(std::move(counts));

		TipSection models{.Title = "Models"};
		AddFamilyRows(models.Rows, model, {session.CostByFamily, session.RequestsByFamily, session.UnpricedByFamily},
					  session.Cost, TipMoney);
		if (!models.Rows.empty())
		{
			card.Sections.push_back(std::move(models));
		}

		TipSection tools = ToolsOf(summary);
		if (!tools.Rows.empty())
		{
			card.Sections.push_back(std::move(tools));
		}

		CloseTip(card, "Click to open its tab");
		return card;
	}

	TipCard ToolTip(const DashboardModel& model, std::size_t toolIndex)
	{
		if (toolIndex >= model.Tools.size())
		{
			return {};
		}

		const NamedTool& tool = model.Tools[toolIndex];
		const auto calls = static_cast<double>(tool.Tally.Calls);
		const auto failed = static_cast<double>(tool.Tally.Failed);
		const std::size_t allCalls = std::accumulate(model.Tools.begin(), model.Tools.end(), std::size_t{0},
													 [](std::size_t sum, const NamedTool& named) { return sum + named.Tally.Calls; });
		TipCard card{.Head = tool.Name, .HeadValue = TipCounted(tool.Tally.Calls, "call", "calls")};

		TipSection facts;
		facts.Rows.push_back({"Share of all calls", TipShare(calls, static_cast<double>(allCalls))});
		facts.Rows.push_back({"Sessions using it", TipCount(tool.Sessions), "of " + TipCount(model.Sessions.size())});
		facts.Rows.push_back({"Per session that used it",
							  TipRounded(calls / static_cast<double>(std::max<std::size_t>(1, tool.Sessions)))});
		facts.Rows.push_back({"Failed", TipCount(tool.Tally.Failed), TipShare(failed, calls),
							  failed > kBadFailureRate * calls ? TipTone::Bad : TipTone::Dim});
		card.Sections.push_back(std::move(facts));
		return card;
	}

	TipCard SkillTip(const DashboardModel& model, std::size_t skillIndex, const LocalCalendar& calendar)
	{
		if (skillIndex >= model.Skills.size())
		{
			return {};
		}

		const NamedSkill& skill = model.Skills[skillIndex];
		TipCard card{.Head = skill.Name, .HeadValue = TipCounted(skill.Tally.Calls, "call", "calls")};

		TipSection facts;
		facts.Rows.push_back({"Sessions using it", TipCount(skill.Sessions), "of " + TipCount(model.Sessions.size())});
		facts.Rows.push_back({"Last used", Moment(skill.Tally.LastCalledAt, calendar)});
		card.Sections.push_back(std::move(facts));
		return card;
	}
}
