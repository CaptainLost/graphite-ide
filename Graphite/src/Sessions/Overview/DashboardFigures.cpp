#include "Sessions/Overview/DashboardFigures.h"

#include <algorithm>
#include <map>
#include <tuple>
#include <utility>

namespace Graphite {

	namespace {
		using Date = std::chrono::year_month_day;

		constexpr int kDaysPerWeek = 7;
		constexpr std::size_t kQuarters = 4;
		constexpr int kTopHeatLevel = 4;

		struct HourCost
		{
			int Hour = 0;
			double Cost = 0;
			bool Unpriced = false;
		};

		using HourCosts = std::vector<HourCost>; // by hour

		struct SessionDay
		{
			std::size_t Session = 0;
			HourCosts Hours;
		};

		std::size_t PickLane(std::vector<int>& laneEnds, int firstHour, int endHour)
		{
			for (std::size_t lane = 0; lane < laneEnds.size(); ++lane)
			{
				if (laneEnds[lane] <= firstHour)
				{
					laneEnds[lane] = endHour;
					return lane;
				}
			}

			if (laneEnds.size() < kTimelineLanes)
			{
				laneEnds.push_back(endHour);
				return laneEnds.size() - 1;
			}

			const auto soonest = std::ranges::min_element(laneEnds);
			*soonest = std::max(*soonest, endHour);
			return static_cast<std::size_t>(soonest - laneEnds.begin());
		}

		void AddRuns(const SessionDay& session, std::size_t lane, std::vector<TimelineBar>& bars)
		{
			for (std::size_t index = 0; index < session.Hours.size();)
			{
				TimelineBar bar{.Session = session.Session, .FirstHour = session.Hours[index].Hour, .EndHour = 0, .Lane = lane};
				int next = bar.FirstHour;

				while (index < session.Hours.size() && session.Hours[index].Hour == next)
				{
					bar.Cost += session.Hours[index].Cost;
					bar.Unpriced = bar.Unpriced || session.Hours[index].Unpriced;
					++next;
					++index;
				}

				bar.EndHour = next;
				bars.push_back(bar);
			}
		}
	}

	std::optional<std::size_t> DayIndex(const DashboardModel& model, Date day)
	{
		if (model.Days.empty())
		{
			return std::nullopt;
		}

		const auto offset = (std::chrono::sys_days{day} - std::chrono::sys_days{model.Days.front().Day}).count();

		if (offset < 0 || static_cast<std::size_t>(offset) >= model.Days.size())
		{
			return std::nullopt;
		}

		return static_cast<std::size_t>(offset);
	}

	double CostOn(const DashboardModel& model, Date day)
	{
		const std::optional<std::size_t> index = DayIndex(model, day);
		return index ? model.Days[*index].Cost : 0.0;
	}

	double Median(std::vector<double> values)
	{
		if (values.empty())
		{
			return 0.0;
		}

		std::sort(values.begin(), values.end());
		const std::size_t middle = values.size() / 2;

		if (values.size() % 2 == 1)
		{
			return values[middle];
		}

		return (values[middle - 1] + values[middle]) / 2.0;
	}

	ActiveDayStats ActiveDays(const DashboardModel& model)
	{
		ActiveDayStats stats;
		std::vector<double> costs;
		double total = 0.0;

		for (std::size_t index = 0; index < model.Days.size(); ++index)
		{
			const double cost = model.Days[index].Cost;

			if (cost <= 0.0)
			{
				continue;
			}

			if (costs.empty() || cost < model.Days[stats.Cheapest].Cost)
			{
				stats.Cheapest = index;
			}

			if (costs.empty() || cost > model.Days[stats.Dearest].Cost)
			{
				stats.Dearest = index;
			}

			costs.push_back(cost);
			total += cost;
		}

		stats.Active = costs.size();
		stats.Idle = model.Days.size() - stats.Active;
		stats.Average = costs.empty() ? 0.0 : total / static_cast<double>(costs.size());
		stats.Median = Median(std::move(costs));
		return stats;
	}

	std::vector<WeekDay> ThisWeek(const DashboardModel& model)
	{
		const std::chrono::sys_days today{model.Today};
		const int mondayBased = static_cast<int>(std::chrono::weekday{today}.iso_encoding()) - 1;
		const std::chrono::days week{kDaysPerWeek};

		std::vector<WeekDay> result;

		for (std::chrono::sys_days day = today - std::chrono::days{mondayBased}; day <= today; day += std::chrono::days{1})
		{
			result.push_back({Date{day}, CostOn(model, Date{day}), CostOn(model, Date{day - week})});
		}

		return result;
	}

	std::optional<std::size_t> BusiestDay(const DashboardModel& model)
	{
		std::optional<std::size_t> busiest;

		for (std::size_t index = 0; index < model.Days.size(); ++index)
		{
			if (model.Days[index].Cost > 0.0 && (!busiest || model.Days[index].Cost > model.Days[*busiest].Cost))
			{
				busiest = index;
			}
		}

		return busiest;
	}

	std::size_t Streak(const DashboardModel& model)
	{
		std::chrono::sys_days day{model.Today};

		if (CostOn(model, model.Today) <= 0.0)
		{
			day -= std::chrono::days{1};
		}

		std::size_t streak = 0;

		while (CostOn(model, Date{day}) > 0.0)
		{
			++streak;
			day -= std::chrono::days{1};
		}

		return streak;
	}

	std::array<double, 3> HeatCuts(const DashboardModel& model)
	{
		std::vector<double> costs;

		for (const DayTotals& day : model.Days)
		{
			if (day.Cost > 0.0)
			{
				costs.push_back(day.Cost);
			}
		}

		std::array<double, 3> cuts{};

		if (costs.empty())
		{
			return cuts;
		}

		std::sort(costs.begin(), costs.end());

		for (std::size_t cut = 0; cut < cuts.size(); ++cut)
		{
			cuts[cut] = costs[std::min(costs.size() - 1, costs.size() * (cut + 1) / kQuarters)];
		}

		return cuts;
	}

	int HeatLevel(double cost, const std::array<double, 3>& cuts, double highest)
	{
		if (cost <= 0.0)
		{
			return 0;
		}

		if (cost >= highest)
		{
			return kTopHeatLevel;
		}

		int level = 1;

		for (const double cut : cuts)
		{
			if (cost <= cut)
			{
				return level;
			}

			++level;
		}

		return level;
	}

	std::vector<std::size_t> SessionsActiveOn(const DashboardModel& model, Date day)
	{
		std::vector<std::pair<double, std::size_t>> active;

		for (std::size_t index = 0; index < model.Sessions.size(); ++index)
		{
			const std::map<Date, double>& costByDay = model.Sessions[index].CostByDay;

			if (const auto found = costByDay.find(day); found != costByDay.end())
			{
				active.emplace_back(found->second, index);
			}
		}

		std::stable_sort(active.begin(), active.end(),
						 [](const auto& left, const auto& right) { return left.first > right.first; });

		std::vector<std::size_t> result;
		for (const auto& [cost, index] : active)
		{
			result.push_back(index);
		}

		return result;
	}

	std::vector<TimelineDay> TimelineDays(const DashboardModel& model, std::size_t count)
	{
		using std::chrono::days;
		using std::chrono::sys_days;

		std::vector<TimelineDay> result;
		if (count == 0)
		{
			return result;
		}

		const sys_days today{model.Today};
		const sys_days oldestShown = today - days{static_cast<long long>(count - 1)};
		std::map<Date, std::map<std::size_t, HourCosts>> byDay;

		for (std::size_t index = 0; index < model.Sessions.size(); ++index)
		{
			const SessionFacts& session = model.Sessions[index];

			for (auto entry = session.CostByHour.lower_bound(DayHour{.Day = Date{oldestShown}, .Hour = 0});
				 entry != session.CostByHour.end() && sys_days{entry->first.Day} <= today; ++entry)
			{
				byDay[entry->first.Day][index].push_back(
					{.Hour = entry->first.Hour, .Cost = entry->second, .Unpriced = session.UnpricedHours.contains(entry->first)});
			}
		}

		const std::optional<sys_days> oldestOnDisk =
			model.Days.empty() ? std::nullopt : std::optional<sys_days>{sys_days{model.Days.front().Day}};

		for (std::size_t offset = 0; offset < count; ++offset)
		{
			const sys_days date = today - days{static_cast<long long>(offset)};
			TimelineDay& day = result.emplace_back();
			day.Day = Date{date};
			day.Gone = !oldestOnDisk || date < *oldestOnDisk;

			const auto found = byDay.find(day.Day);
			if (found == byDay.end())
			{
				continue;
			}

			std::vector<SessionDay> sessions;
			for (auto& [session, hours] : found->second)
			{
				sessions.push_back({.Session = session, .Hours = std::move(hours)});
			}

			std::ranges::stable_sort(sessions, {}, [](const SessionDay& session) { return session.Hours.front().Hour; });

			std::vector<int> laneEnds;
			for (const SessionDay& session : sessions)
			{
				const std::size_t lane = PickLane(laneEnds, session.Hours.front().Hour, session.Hours.back().Hour + 1);
				AddRuns(session, lane, day.Bars);
			}

			day.Lanes = laneEnds.size();
			std::ranges::stable_sort(day.Bars, [](const TimelineBar& left, const TimelineBar& right) {
				return std::tie(left.Lane, left.FirstHour) < std::tie(right.Lane, right.FirstHour);
			});
		}

		return result;
	}

	ModelFamily MainFamily(const SessionFacts& session)
	{
		const bool costed = std::ranges::any_of(session.CostByFamily, [](double cost) { return cost > 0.0; });
		const bool requested = std::ranges::any_of(session.RequestsByFamily, [](std::size_t requests) { return requests > 0; });

		if (!costed && !requested)
		{
			return ModelFamily::Other;
		}

		std::size_t best = 0;

		for (std::size_t family = 1; family < session.CostByFamily.size(); ++family)
		{
			const bool ahead = costed ? session.CostByFamily[family] > session.CostByFamily[best]
									  : session.RequestsByFamily[family] > session.RequestsByFamily[best];
			best = ahead ? family : best;
		}

		return static_cast<ModelFamily>(best);
	}
}
