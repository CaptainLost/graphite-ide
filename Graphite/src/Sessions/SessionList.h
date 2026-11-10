#pragma once

#include "Sessions/Overview/DashboardColours.h"
#include "Sessions/Overview/DashboardModel.h"
#include "Sessions/Overview/DashboardSource.h"
#include "Sessions/Overview/LocalCalendar.h"
#include "Sessions/SessionSummary.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Graphite {

	class LocalClock;
	class Pricing;

	bool ActiveOnDay(const SessionSummary& summary, std::chrono::year_month_day day, const LocalCalendar& calendar);

	std::string DayChipText(std::chrono::year_month_day day, std::size_t sessions); // "Sun 11 Oct · 4"

	struct SessionRowText
	{
		std::string Title;
		std::string Worktree;
		std::string Detail;
	};

	enum class SessionRowAction
	{
		None,
		Open,
		Continue
	};

	struct SessionRowResult
	{
		SessionRowAction Action = SessionRowAction::None;
		bool Hovered = false;
	};

	SessionRowResult DrawSessionRow(const SessionRowText& text);

	struct SessionListFilter
	{
		std::string Title;
		std::optional<std::chrono::year_month_day> Day;
		bool Grouped = true;
	};

	struct SessionListRow
	{
		const ListedSession* Session = nullptr;
		std::filesystem::path::string_type Group;
	};

	using IsSessionOpen = std::function<bool(const SessionSummary&)>;

	std::vector<SessionListRow> SessionListRows(std::span<const ListedSession> sessions, const DashboardModel& model,
												const SessionListFilter& filter, const IsSessionOpen& isOpen,
												const LocalCalendar& calendar);

	std::size_t SessionsOnDay(std::span<const ListedSession> sessions, const DashboardModel& model,
							  std::chrono::year_month_day day, const LocalCalendar& calendar);

	struct SessionListContext
	{
		const Pricing& Prices;
		const DashboardModel& Model;
		const LocalCalendar& Calendar;
		const DashboardColours& Colours;
		LocalClock& Clock;
	};

	struct SessionListAction
	{
		SessionRowAction Action = SessionRowAction::None;
		const SessionSummary* Summary = nullptr;
	};

	// WARNING: Fills the rest of the current window with a scrolling child of its own.
	SessionListAction DrawSessionList(std::span<const SessionListRow> rows, bool grouped, const SessionListContext& context);
}
