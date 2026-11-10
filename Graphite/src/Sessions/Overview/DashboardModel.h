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
		std::size_t Requests = 0;
		std::size_t Prompts = 0;
		std::size_t SessionsStarted = 0;
		std::uint64_t Output = 0;
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
		TokenTotals Tokens;
		std::chrono::year_month_day Today;
	};

	// WARNING: An hour keeps one request count for all its models, so a family's requests are the hour's
	// divided by each model's share of the hour's tokens; the shares always add up to the hour's count.
	DashboardModel BuildDashboard(std::span<const SessionSummary* const> sessions, const Pricing& pricing,
								  const LocalCalendar& calendar, std::chrono::year_month_day today);
}
