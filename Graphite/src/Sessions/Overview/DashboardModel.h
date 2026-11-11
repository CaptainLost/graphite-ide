#pragma once

#include "Sessions/Overview/LocalCalendar.h"
#include "Sessions/Overview/SessionPlace.h"
#include "Sessions/Pricing.h"
#include "Sessions/SessionActivity.h"
#include "Sessions/SessionSummary.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	enum class ModelFamily : std::uint8_t
	{
		Opus,
		Sonnet,
		Haiku,
		Fable,
		Other,
		Count
	};

	inline constexpr std::size_t kModelFamilyCount = static_cast<std::size_t>(ModelFamily::Count);

	ModelFamily FamilyOf(std::string_view model);

	std::string_view FamilyName(ModelFamily family);

	using FamilyCosts = std::array<double, kModelFamilyCount>;

	using FamilyCounts = std::array<std::size_t, kModelFamilyCount>;

	// WARNING: Net of the write premium, so a session that wrote to the cache and never read it back saves
	// less than nothing; a model without a price saves $0.
	double CacheSaving(const UsageKey& key, const TokenCounts& counts, const Pricing& pricing);

	struct TokenTotals
	{
		std::uint64_t Input = 0;
		std::uint64_t Output = 0;
		std::uint64_t CacheWrite = 0;
		std::uint64_t CacheRead = 0;
	};

	struct DayTotals
	{
		std::chrono::year_month_day Day;
		double Cost = 0;
		double SubagentCost = 0;
		double Saved = 0;
		FamilyCosts CostByFamily{};
		// WARNING: The requests of a model without a price count here and cost $0 in the figures above, so a reader
		// shows "?" for their family rather than a cost that leaves them out.
		FamilyCounts RequestsByFamily{};
		FamilyCounts UnpricedByFamily{};
		std::set<std::string> UnpricedModels;
		std::size_t Requests = 0;
		std::size_t Prompts = 0;
		std::size_t SessionsStarted = 0;
		std::uint64_t Output = 0;
	};

	struct DayHour
	{
		std::chrono::year_month_day Day;
		int Hour = 0; // local, 0 .. 23

		friend auto operator<=>(const DayHour&, const DayHour&) = default;
	};

	struct SessionFacts
	{
		const SessionSummary* Summary = nullptr;
		SessionPlace Place;
		double Cost = 0;
		double SubagentCost = 0;
		FamilyCosts CostByFamily{};
		// WARNING: Holds every local day with any usage, at $0 too, since that is what makes a session active
		// on the day.
		std::map<std::chrono::year_month_day, double> CostByDay;
		// WARNING: Holds every local hour with any usage, at $0 too, as CostByDay holds days; subagents' hours count
		// as the session's own.
		std::map<DayHour, double> CostByHour;
		std::set<DayHour> UnpricedHours; // with a request to a model without a price
		FamilyCounts RequestsByFamily{};
		FamilyCounts UnpricedByFamily{};
		std::set<std::string> UnpricedModels;
		std::size_t UnpricedRequests = 0;
		std::size_t Requests = 0;
		std::size_t Prompts = 0;
		std::uint64_t Output = 0;
	};

	struct ProjectTotals
	{
		std::filesystem::path Repository;
		double Cost = 0;
		double OpusCost = 0;
		double SubagentCost = 0;
		std::size_t Sessions = 0;
		std::set<std::string> Worktrees;
		std::vector<double> DailyCost; // aligned with DashboardModel::Days
		std::optional<std::chrono::year_month_day> LastActive;
		std::size_t MostExpensiveSession = 0; // index into DashboardModel::Sessions
	};

	struct FamilyTotals
	{
		double Cost = 0;
		double SubagentCost = 0;
		double Saved = 0;
		std::size_t Requests = 0;
		std::size_t SubagentRequests = 0;
		std::size_t UnpricedRequests = 0;
		std::size_t UnpricedSubagentRequests = 0;
		std::map<std::string, std::size_t> RequestsByModel; // by model id
		std::set<std::string> UnpricedModels;
		TokenTotals Tokens;
	};

	struct NamedTool
	{
		std::string Name;
		ToolTally Tally;
		std::size_t Sessions = 0;
	};

	struct NamedSkill
	{
		std::string Name;
		SkillTally Tally;
		std::size_t Sessions = 0;
	};

	struct HourCell
	{
		double Cost = 0;
		std::size_t Requests = 0;
	};

	inline constexpr std::size_t kTimelineLanes = 4;
	inline constexpr std::size_t kTimelineMostDays = 91; // the map's 13 weeks

	// One run of a session's consecutive local hours on one day.
	struct TimelineBar
	{
		std::size_t Session = 0; // index into DashboardModel::Sessions
		int FirstHour = 0;
		int EndHour = 0; // one past the last
		std::size_t Lane = 0;
		double Cost = 0;
		bool Unpriced = false; // an hour of it has a request to a model without a price
	};

	struct TimelineDay
	{
		std::chrono::year_month_day Day;
		bool Gone = false;			   // older than the oldest day on disk
		std::size_t Lanes = 0;		   // 0 .. kTimelineLanes
		std::vector<TimelineBar> Bars; // by lane, then by first hour
	};

	struct DashboardModel
	{
		std::vector<DayTotals> Days; // oldest day with usage .. today, no gaps
		std::vector<SessionFacts> Sessions; // by cost, descending
		std::vector<ProjectTotals> Projects; // by cost, descending
		std::array<FamilyTotals, kModelFamilyCount> Families{};
		std::vector<NamedTool> Tools; // by calls, descending
		std::vector<NamedSkill> Skills; // by calls, descending
		std::array<std::array<HourCell, 24>, 7> Punch{}; // [weekday, Monday = 0][local hour]
		double Cost = 0;
		double SubagentCost = 0;
		double Saved = 0;
		std::size_t Requests = 0;
		std::size_t Prompts = 0;
		std::size_t Compactions = 0;
		std::size_t SubagentRuns = 0;
		std::size_t UnpricedRequests = 0;
		std::set<std::string> UnpricedModels;
		TokenTotals Tokens;
		std::chrono::year_month_day Today;
		std::vector<TimelineDay> Timeline; // today first, kTimelineMostDays of them
	};

	DashboardModel BuildDashboard(std::span<const SessionSummary* const> sessions, const Pricing& pricing,
								  const LocalCalendar& calendar, std::chrono::year_month_day today);
}
