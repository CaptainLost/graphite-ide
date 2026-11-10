#pragma once

#include "Sessions/Overview/DashboardColours.h"
#include "Sessions/Overview/DashboardLayout.h"
#include "Sessions/Overview/DashboardModel.h"
#include "Sessions/Overview/LocalCalendar.h"
#include "Sessions/Overview/TipCardView.h"

#include <chrono>
#include <cstddef>
#include <optional>

namespace Graphite {

	struct DashboardAction
	{
		std::optional<std::chrono::year_month_day> ShowDay;
		std::optional<std::size_t> OpenSession; // index into DashboardModel::Sessions
	};

	// WARNING: Fills the rest of the current window with a scrolling child of its own; do not wrap it in another.
	DashboardAction DrawDashboard(const DashboardModel& model, const LocalCalendar& calendar, const DashboardColours& colours);

	// WARNING: Measured at the current font size, relative to the dashboard's top left corner.
	DashboardGrid LayOutDashboard(const DashboardModel& model, float width);
}
