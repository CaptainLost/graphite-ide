#pragma once

#include "Sessions/Overview/DashboardModel.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

namespace Graphite {

	std::optional<std::size_t> DayIndex(const DashboardModel& model, std::chrono::year_month_day day); // index into DashboardModel::Days

	double CostOn(const DashboardModel& model, std::chrono::year_month_day day); // $0 outside DashboardModel::Days

	struct ActiveDayStats
	{
		double Average = 0;
		double Median = 0;
		std::size_t Cheapest = 0; // index into DashboardModel::Days
		std::size_t Dearest = 0;  // index into DashboardModel::Days
		std::size_t Active = 0;
		std::size_t Idle = 0;
	};

	ActiveDayStats ActiveDays(const DashboardModel& model);

	struct WeekDay
	{
		std::chrono::year_month_day Day;
		double Cost = 0;
		double WeekBefore = 0;
	};

	std::vector<WeekDay> ThisWeek(const DashboardModel& model); // Monday .. today

	std::optional<std::size_t> BusiestDay(const DashboardModel& model); // index into DashboardModel::Days

	std::size_t Streak(const DashboardModel& model); // days with cost in a row, ending today or, when today has none, yesterday

	std::array<double, 3> HeatCuts(const DashboardModel& model); // quarter points of the non-zero day costs, by nearest rank

	int HeatLevel(double cost, const std::array<double, 3>& cuts); // 0 .. 4

	std::vector<std::size_t> SessionsActiveOn(const DashboardModel& model, std::chrono::year_month_day day); // by that day's cost, descending

	double Median(std::vector<double> values);
}
