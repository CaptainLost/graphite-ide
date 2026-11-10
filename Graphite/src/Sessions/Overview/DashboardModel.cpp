#include "Sessions/Overview/DashboardModel.h"

#include "Core/FileSystem/FileSystem.h"

#include "Sessions/Overview/DashboardFigures.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace Graphite {

	namespace {
		using Date = std::chrono::year_month_day;
		using RepositoryKey = std::filesystem::path::string_type;

		constexpr std::string_view kModelPrefix = "claude-";
		constexpr std::string_view kSubagentHandback = "SubagentHandback";

		struct FamilyEntry
		{
			std::string_view Name;
			std::string_view Prefix;
		};

		// WARNING: Indexed by ModelFamily, so the order is the enum's.
		constexpr std::array<FamilyEntry, kModelFamilyCount> kFamilies = {{
			{"Opus", "opus"},
			{"Sonnet", "sonnet"},
			{"Haiku", "haiku"},
			{"Fable", "fable"},
			{"Other", ""},
		}};

		std::size_t IndexOf(ModelFamily family)
		{
			return static_cast<std::size_t>(family);
		}

		bool NamesFamily(std::string_view model, std::string_view prefix)
		{
			if (prefix.empty() || !model.starts_with(kModelPrefix))
			{
				return false;
			}

			const std::string_view rest = model.substr(kModelPrefix.size());

			if (!rest.starts_with(prefix))
			{
				return false;
			}

			const std::string_view after = rest.substr(prefix.size());
			return after.empty() || after.front() == '-' || after.front() == '[';
		}

		void AddTokens(TokenTotals& tokens, const TokenCounts& counts)
		{
			tokens.Input += counts.Input;
			tokens.Output += counts.Output;
			tokens.CacheWrite += counts.CacheWrite5m + counts.CacheWrite1h;
			tokens.CacheRead += counts.CacheRead;
		}

		enum class Thread
		{
			Main,
			Subagent
		};

		struct Accumulator
		{
			const Pricing& Prices;
			const LocalCalendar& Calendar;
			DashboardModel& Model;
			std::map<Date, DayTotals> Days;

			DayTotals& DayAt(Date date)
			{
				DayTotals& totals = Days[date];
				totals.Day = date;
				return totals;
			}

			void AddHours(const std::map<UtcHour, HourActivity>& hours, Thread thread, SessionFacts& session)
			{
				for (const auto& [utcHour, activity] : hours)
				{
					AddHour(Calendar.At(utcHour), activity, thread, session);
				}
			}

			void AddHour(const LocalHour& local, const HourActivity& activity, Thread thread, SessionFacts& session)
			{
				DayTotals& day = DayAt(local.Day);
				HourCell& cell = Model.Punch[static_cast<std::size_t>(local.Weekday)][static_cast<std::size_t>(local.Hour)];

				day.Requests += activity.Requests;
				cell.Requests += activity.Requests;
				session.Requests += activity.Requests;
				Model.Requests += activity.Requests;

				if (thread == Thread::Main)
				{
					day.Prompts += activity.Prompts;
					session.Prompts += activity.Prompts;
					Model.Prompts += activity.Prompts;
				}

				for (const auto& [key, requests] : activity.RequestsByModel)
				{
					Model.Families[IndexOf(FamilyOf(key.Model))].Requests += requests;

					if (!Prices.BucketsOf(key, {}))
					{
						Model.UnpricedRequests += requests;
					}
				}

				for (const auto& [key, counts] : activity.Usage)
				{
					const std::optional<BucketCosts> buckets = Prices.BucketsOf(key, counts);
					const double cost = buckets ? buckets->Total() : 0.0;
					const double subagentCost = thread == Thread::Subagent ? cost : 0.0;
					const double saved = CacheSaving(key, counts, Prices);
					const std::size_t family = IndexOf(FamilyOf(key.Model));

					day.Cost += cost;
					day.SubagentCost += subagentCost;
					day.Saved += saved;
					day.CostByFamily[family] += cost;
					day.Output += counts.Output;

					cell.Cost += cost;

					session.Cost += cost;
					session.SubagentCost += subagentCost;
					session.CostByFamily[family] += cost;
					session.CostByDay[local.Day] += cost;
					session.Output += counts.Output;

					FamilyTotals& familyTotals = Model.Families[family];
					familyTotals.Cost += cost;
					familyTotals.SubagentCost += subagentCost;
					familyTotals.Saved += saved;
					AddTokens(familyTotals.Tokens, counts);

					Model.Cost += cost;
					Model.SubagentCost += subagentCost;
					Model.Saved += saved;
					AddTokens(Model.Tokens, counts);
				}
			}

			// WARNING: Runs to the newest day with usage when that is after today, so a clock set back never
			// drops a day the totals still count.
			std::vector<DayTotals> ContiguousDays(Date today)
			{
				std::vector<DayTotals> result;

				if (Days.empty())
				{
					return result;
				}

				const std::chrono::sys_days first{Days.begin()->first};
				const std::chrono::sys_days last = std::max(std::chrono::sys_days{Days.rbegin()->first}, std::chrono::sys_days{today});

				for (std::chrono::sys_days date = first; date <= last; date += std::chrono::days{1})
				{
					result.push_back(DayAt(Date{date}));
				}

				return result;
			}
		};

		void AddSessionsStarted(std::span<const SessionSummary* const> sessions, const LocalCalendar& calendar,
								DashboardModel& model)
		{
			for (const SessionSummary* summary : sessions)
			{
				const std::optional<Date> date = calendar.DayOf(summary->StartedAt);

				if (!date)
				{
					continue;
				}

				if (const std::optional<std::size_t> index = DayIndex(model, *date))
				{
					++model.Days[*index].SessionsStarted;
				}
			}
		}

		bool HasTokens(const UsageByModel& usage)
		{
			return std::any_of(usage.begin(), usage.end(), [](const auto& entry) { return entry.second.Total() > 0; });
		}

		void AddWorktrees(const SessionFacts& session, const RepositoryKey& repositoryKey, ProjectTotals& project)
		{
			if (!session.Place.Worktree.empty())
			{
				project.Worktrees.insert(session.Place.Worktree);
			}

			for (const auto& [cwd, usage] : session.Summary->Activity.MainUsageByCwd)
			{
				if (cwd.empty() || !HasTokens(usage))
				{
					continue;
				}

				const SessionPlace place = PlaceOfDirectory(Core::FromUtf8(cwd));

				if (!place.Worktree.empty() && DirectoryKey(place.Repository) == repositoryKey)
				{
					project.Worktrees.insert(place.Worktree);
				}
			}
		}

		// WARNING: Expects the sessions sorted by cost, so a project's first member is its most expensive one
		// and names it.
		std::vector<ProjectTotals> GroupProjects(const DashboardModel& model)
		{
			std::vector<ProjectTotals> projects;
			std::unordered_map<RepositoryKey, std::size_t> indexByKey;

			for (std::size_t sessionIndex = 0; sessionIndex < model.Sessions.size(); ++sessionIndex)
			{
				const SessionFacts& session = model.Sessions[sessionIndex];
				const RepositoryKey key = DirectoryKey(session.Place.Repository);
				const auto [entry, isNew] = indexByKey.try_emplace(key, projects.size());

				if (isNew)
				{
					ProjectTotals project;
					project.Repository = session.Place.Repository;
					project.DailyCost.assign(model.Days.size(), 0.0);
					project.MostExpensiveSession = sessionIndex;
					projects.push_back(std::move(project));
				}

				ProjectTotals& project = projects[entry->second];
				project.Cost += session.Cost;
				project.OpusCost += session.CostByFamily[IndexOf(ModelFamily::Opus)];
				project.SubagentCost += session.SubagentCost;
				++project.Sessions;

				for (const auto& [date, cost] : session.CostByDay)
				{
					if (const std::optional<std::size_t> dayIndex = DayIndex(model, date))
					{
						project.DailyCost[*dayIndex] += cost;
					}

					project.LastActive = project.LastActive ? std::max(*project.LastActive, date) : date;
				}

				AddWorktrees(session, key, project);
			}

			std::stable_sort(projects.begin(), projects.end(),
							 [](const ProjectTotals& left, const ProjectTotals& right) { return left.Cost > right.Cost; });
			return projects;
		}

		template <typename Named, typename Tally, typename Merge>
		std::vector<Named> CollectNamed(std::span<const SessionSummary* const> sessions,
										std::map<std::string, Tally> SessionActivity::*tallies, Merge merge)
		{
			std::map<std::string, Named> byName;

			for (const SessionSummary* summary : sessions)
			{
				for (const auto& [name, tally] : summary->Activity.*tallies)
				{
					if (name == kSubagentHandback || tally.Calls == 0)
					{
						continue;
					}

					Named& named = byName[name];
					named.Name = name;
					merge(named.Tally, tally);
					++named.Sessions;
				}
			}

			std::vector<Named> result;
			for (auto& [name, named] : byName)
			{
				result.push_back(std::move(named));
			}

			std::stable_sort(result.begin(), result.end(),
							 [](const Named& left, const Named& right) { return left.Tally.Calls > right.Tally.Calls; });
			return result;
		}
	}

	ModelFamily FamilyOf(std::string_view model)
	{
		for (std::size_t index = 0; index < kFamilies.size(); ++index)
		{
			if (NamesFamily(model, kFamilies[index].Prefix))
			{
				return static_cast<ModelFamily>(index);
			}
		}

		return ModelFamily::Other;
	}

	std::string_view FamilyName(ModelFamily family)
	{
		const std::size_t index = std::min(IndexOf(family), IndexOf(ModelFamily::Other));
		return kFamilies[index].Name;
	}

	double CacheSaving(const UsageKey& key, const TokenCounts& counts, const Pricing& pricing)
	{
		const std::optional<BucketCosts> actual = pricing.BucketsOf(key, counts);

		if (!actual)
		{
			return 0.0;
		}

		TokenCounts uncached;
		uncached.Input = counts.Input + counts.CacheWrite5m + counts.CacheWrite1h + counts.CacheRead;
		const std::optional<BucketCosts> withoutCache = pricing.BucketsOf(key, uncached);

		return withoutCache->Input - (actual->Input + actual->CacheWrite + actual->CacheRead);
	}

	DashboardModel BuildDashboard(std::span<const SessionSummary* const> sessions, const Pricing& pricing,
								  const LocalCalendar& calendar, std::chrono::year_month_day today)
	{
		DashboardModel model;
		model.Today = today;
		Accumulator accumulator{pricing, calendar, model, {}};

		for (const SessionSummary* summary : sessions)
		{
			SessionFacts session;
			session.Summary = summary;
			session.Place = PlaceOfSession(*summary, pricing);

			accumulator.AddHours(summary->Activity.MainHours, Thread::Main, session);
			accumulator.AddHours(summary->Activity.SubagentHours, Thread::Subagent, session);

			model.Compactions += summary->Activity.Compactions;
			model.SubagentRuns += summary->SubagentCount;
			model.Sessions.push_back(std::move(session));
		}

		model.Days = accumulator.ContiguousDays(today);
		AddSessionsStarted(sessions, calendar, model);

		std::stable_sort(model.Sessions.begin(), model.Sessions.end(),
						 [](const SessionFacts& left, const SessionFacts& right) { return left.Cost > right.Cost; });

		model.Projects = GroupProjects(model);

		model.Tools = CollectNamed<NamedTool>(sessions, &SessionActivity::Tools, [](ToolTally& into, const ToolTally& from) {
			into.Calls += from.Calls;
			into.Failed += from.Failed;
		});

		model.Skills = CollectNamed<NamedSkill>(sessions, &SessionActivity::Skills, [](SkillTally& into, const SkillTally& from) {
			into.Calls += from.Calls;
			into.LastCalledAt = std::max(into.LastCalledAt, from.LastCalledAt);
		});

		return model;
	}
}
