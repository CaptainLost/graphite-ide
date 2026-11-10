#include "Sessions/Overview/DashboardFigures.h"

#include <algorithm>
#include <utility>

namespace Graphite {

	namespace {
		using Date = std::chrono::year_month_day;

		constexpr int kDaysPerWeek = 7;
		constexpr std::size_t kQuarters = 4;
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

	int HeatLevel(double cost, const std::array<double, 3>& cuts)
	{
		if (cost <= 0.0)
		{
			return 0;
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
}
