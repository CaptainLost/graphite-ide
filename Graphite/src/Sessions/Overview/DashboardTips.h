#pragma once

#include "Sessions/Overview/DashboardFigures.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	enum class TipTone : std::uint8_t
	{
		Normal,
		Dim,
		Bad
	};

	struct TipRow
	{
		std::string Label;
		std::string Value;
		std::string Extra;
		TipTone ExtraTone = TipTone::Dim;
		std::optional<ModelFamily> Swatch;
	};

	struct TipSection
	{
		std::string Title;
		std::vector<TipRow> Rows;
		std::vector<std::string> Notes; // under the rows
	};

	// WARNING: Notes stand under the head and Body, before the first section; a note that closes the card is
	// the last section's note.
	struct TipCard
	{
		std::string Head;
		std::string HeadValue;
		std::string Banner; // the hovered sparkline day, may be empty
		std::string Body;	// a full title or path, may be empty
		std::vector<std::string> Notes;
		std::vector<TipSection> Sections;
	};

	enum class NumberTile : std::uint8_t
	{
		Spent,
		ThisWeek,
		PerActiveDay,
		Sessions,
		Subagents,
		CacheSaved,
		Output,
		BusiestDay
	};

	enum class CachePart : std::uint8_t
	{
		Read,
		Written,
		Uncached
	};

	// hoveredDay is an index into DashboardModel::Days
	TipCard NumberTip(const DashboardModel& model, NumberTile tile, std::optional<std::size_t> hoveredDay);

	TipCard DayTip(const DashboardModel& model, std::size_t dayIndex);

	TipCard GoneDayTip(std::chrono::year_month_day day);

	TipCard ModelTip(const DashboardModel& model, ModelFamily family);

	TipCard ProjectTip(const DashboardModel& model, std::size_t projectIndex);

	TipCard SessionTip(const DashboardModel& model, std::size_t sessionIndex, const LocalCalendar& calendar);

	TipCard ToolTip(const DashboardModel& model, std::size_t toolIndex);

	TipCard SkillTip(const DashboardModel& model, std::size_t skillIndex,
					 const LocalCalendar& calendar = LocalCalendar::Current());

	TipCard PunchTip(const DashboardModel& model, int weekday, int hour); // weekday 0 = Monday, local hour

	TipCard CacheTip(const DashboardModel& model, CachePart part);
}
