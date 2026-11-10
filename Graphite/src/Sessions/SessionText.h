#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Requests/RequestTimeline.h"
#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"
#include "Terminal/CommandTerminal.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	inline constexpr std::string_view kLiveSessionMark = "\xE2\x97\x8F";

	std::string FormatCost(const CostResult& cost);

	std::string UnknownCostHint(const CostResult& cost);

	bool ShowsInSessionList(const SessionSummary& summary, bool isLive);

	std::string ScanProgressText(std::size_t finishedCount, std::size_t totalCount);

	std::string EmptyListMessage(bool isScanning, std::size_t finishedCount, std::size_t totalCount,
								 bool isWorkspaceOnly, std::string_view filter,
								 std::optional<std::chrono::year_month_day> day = std::nullopt);

	bool SessionScanIsUnderway(bool indexing, bool initialScanRunning);

	bool OverviewWaitsForScan(bool indexing, bool initialScanRunning, bool cacheGaveNothing);

	enum class OverviewContent : std::uint8_t
	{
		Loading,
		Empty,
		Dashboard
	};

	// WARNING: Nothing in scope reads as empty only once the scan is done; before that it may be still coming.
	OverviewContent OverviewContentFor(bool indexing, bool initialScanRunning, bool cacheGaveNothing, bool hasSessions);

	std::string OverviewEmptyMessage(bool folderInScope);

	std::optional<std::string> PendingSessionEndText(const CommandTerminal& terminal);

	std::string UnfoundSessionText(std::string_view problem);

	std::string TranscriptLoadingText(std::uint64_t loadedBytes, std::uint64_t totalBytes);

	std::string FormatDuration(std::chrono::milliseconds span);

	std::string CompactionLabel(const CompactionFacts& facts, const std::string& time);

	std::string ToolSummary(const ToolCall& tool);

	std::vector<std::string> EditLines(const ToolCall& tool);

	std::optional<std::filesystem::path> ToolFilePath(const ToolCall& tool);

	struct TurnCost
	{
		BucketCosts Buckets;

		// WARNING: When this is false, Buckets is only part of the cost, and a reader must be shown "?" rather
		// than it.
		bool Known = true;
	};

	std::vector<TurnCost> TurnCosts(const Session& session, const Pricing& pricing);

	struct TurnRequest
	{
		std::string Key;
		std::optional<std::size_t> Position;
		CostResult Cost;
	};

	struct ToolCount
	{
		std::string Name;
		std::size_t Count = 0;
	};

	struct TurnBreakdown
	{
		std::vector<TurnRequest> Requests;
		std::vector<ToolCount> Tools;
	};

	TurnBreakdown BreakDownTurn(const Session& session, std::size_t turnIndex, const RequestTimeline& timeline,
								const Pricing& pricing);

	std::vector<std::string> RequestActivity(const ApiRequest& request);

	std::string SingleLineLabel(std::string_view text, std::size_t maxBytes);
}
