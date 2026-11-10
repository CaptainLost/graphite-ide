#include "Sessions/SessionText.h"

#include "Sessions/Overview/TipText.h"
#include "Sessions/Transcript/JsonAccess.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "Terminal/TerminalRules.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Strings.h"
#include "Core/FileSystem/FileSystem.h"

#include "UI/TypographicMarks.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::size_t kRawSummaryBytes = 80;
		constexpr std::size_t kActivityBytes = 300;

		constexpr double kTraceCostDollars = 0.005;
		constexpr std::chrono::seconds::rep kSecondsPerMinute = 60;
		constexpr std::chrono::seconds::rep kSecondsPerHour = 3600;

		const char* const kRemovedLinePrefix = "- ";
		const char* const kAddedLinePrefix = "+ ";

		constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;

		double Megabytes(std::uint64_t bytes)
		{
			return static_cast<double>(bytes) / kBytesPerMegabyte;
		}

		void AddLines(std::vector<std::string>& lines, const std::string& text, const char* linePrefix)
		{
			std::size_t lineStart = 0;

			while (lineStart <= text.size())
			{
				const std::size_t lineEnd = std::min(text.find('\n', lineStart), text.size());
				lines.push_back(linePrefix + text.substr(lineStart, lineEnd - lineStart));
				lineStart = lineEnd + 1;
			}
		}

		bool IsDearerRequestFirst(const TurnRequest& firstRequest, const TurnRequest& secondRequest)
		{
			if (firstRequest.Cost.IsKnown() != secondRequest.Cost.IsKnown())
			{
				return firstRequest.Cost.IsKnown();
			}
			return firstRequest.Cost.Dollars > secondRequest.Cost.Dollars;
		}

		bool IsMoreUsedToolFirst(const ToolCount& firstTool, const ToolCount& secondTool)
		{
			return firstTool.Count > secondTool.Count;
		}
	}

	std::string FormatCost(const CostResult& cost)
	{
		// WARNING: A partial sum shown as a total would be a wrong number that looks right, so any unknown
		// part makes the whole cost "?".
		if (!cost.IsKnown())
		{
			return "?";
		}

		if (cost.Dollars > 0.0 && cost.Dollars < kTraceCostDollars)
		{
			return "< $0.01";
		}

		return std::format("${:.2f}", cost.Dollars);
	}

	std::string UnknownCostHint(const CostResult& cost)
	{
		if (cost.IsKnown())
		{
			return {};
		}

		return "No price for " + Core::JoinedWith(cost.UnknownModels, ", ") +
			   ". Add a price under Settings > Claude.";
	}

	bool ShowsInSessionList(const SessionSummary& summary, bool isLive)
	{
		return isLive || !summary.IsEmpty();
	}

	std::string ScanProgressText(std::size_t finishedCount, std::size_t totalCount)
	{
		std::string text = "Reading transcripts...";

		if (totalCount > 0)
		{
			text += std::format(" {} / {}", std::min(finishedCount, totalCount), totalCount);
		}

		return text;
	}

	std::string EmptyListMessage(bool isScanning, std::size_t finishedCount, std::size_t totalCount,
								 bool isWorkspaceOnly, std::string_view filter,
								 std::optional<std::chrono::year_month_day> day)
	{
		if (isScanning)
		{
			return ScanProgressText(finishedCount, totalCount);
		}

		if (day.has_value())
		{
			const std::string onDay = "No sessions on " + WeekdayDate(*day);
			return filter.empty() ? onDay : std::format("{} match \"{}\"", onDay, filter);
		}

		if (!filter.empty())
		{
			return std::format("No sessions match \"{}\"", filter);
		}

		return isWorkspaceOnly ? "No sessions in this workspace" : "No sessions.";
	}

	bool SessionScanIsUnderway(bool indexing, bool initialScanRunning)
	{
		const bool scanNotStartedYet = !indexing;
		return scanNotStartedYet || initialScanRunning;
	}

	bool OverviewWaitsForScan(bool indexing, bool initialScanRunning, bool cacheGaveNothing)
	{
		return cacheGaveNothing && SessionScanIsUnderway(indexing, initialScanRunning);
	}

	OverviewContent OverviewContentFor(bool indexing, bool initialScanRunning, bool cacheGaveNothing, bool hasSessions)
	{
		if (OverviewWaitsForScan(indexing, initialScanRunning, cacheGaveNothing))
		{
			return OverviewContent::Loading;
		}

		if (hasSessions)
		{
			return OverviewContent::Dashboard;
		}

		return SessionScanIsUnderway(indexing, initialScanRunning) ? OverviewContent::Loading : OverviewContent::Empty;
	}

	std::string OverviewEmptyMessage(bool folderInScope)
	{
		if (folderInScope)
		{
			return "No Claude sessions in this folder\nUntick \"This workspace only\" to see every folder";
		}

		return "No Claude sessions yet";
	}

	std::optional<std::string> PendingSessionEndText(const CommandTerminal& terminal)
	{
		if (terminal.Exited)
		{
			const std::string exitedText = "Claude exited before starting a session";

			if (!terminal.ExitCode.has_value())
			{
				return exitedText;
			}

			return std::format("{} (code {})", exitedText, ExitCodeText(*terminal.ExitCode));
		}

		if (terminal.Closed)
		{
			return "Claude's terminal was closed before it started a session";
		}

		return std::nullopt;
	}

	std::string UnfoundSessionText(std::string_view problem)
	{
		return std::format("This session can't be found: {}", problem);
	}

	std::string TranscriptLoadingText(std::uint64_t loadedBytes, std::uint64_t totalBytes)
	{
		const std::string loadingText = "Loading transcript";

		if (totalBytes == 0)
		{
			return loadingText;
		}

		return std::format("{}{}{:.1f} / {:.1f} MB", loadingText, kMiddleDotSeparator, Megabytes(loadedBytes),
						   Megabytes(totalBytes));
	}

	std::string FormatDuration(std::chrono::milliseconds span)
	{
		const std::chrono::seconds::rep wholeSeconds = std::chrono::duration_cast<std::chrono::seconds>(span).count();

		if (wholeSeconds < kSecondsPerMinute)
		{
			return std::format("{}s", wholeSeconds);
		}

		if (wholeSeconds < kSecondsPerHour)
		{
			return std::format("{}m", wholeSeconds / kSecondsPerMinute);
		}

		return std::format("{}h {}m", wholeSeconds / kSecondsPerHour,
						   wholeSeconds % kSecondsPerHour / kSecondsPerMinute);
	}

	std::string CompactionLabel(const CompactionFacts& facts, const std::string& time)
	{
		std::vector<std::string> pieces{std::string{"Context compacted"}};

		if (facts.PreTokens && facts.PostTokens)
		{
			pieces.push_back(std::format("{} {} {}", FormatTokens(*facts.PreTokens), kRightwardsArrow,
										 FormatTokens(*facts.PostTokens)));
		}
		if (!facts.Trigger.empty())
		{
			pieces.push_back(facts.Trigger);
		}
		if (facts.Duration)
		{
			pieces.push_back(FormatDuration(*facts.Duration));
		}
		if (!time.empty())
		{
			pieces.push_back(time);
		}

		return Core::JoinedWith(pieces, kMiddleDotSeparator);
	}

	std::string ToolSummary(const ToolCall& tool)
	{
		const nlohmann::json input = ObjectOf(tool.InputJson);

		if (tool.Name == "Agent")
		{
			const std::string task = StringAt(input, "description");
			const std::string agentType = StringAt(input, "subagent_type");

			if (!task.empty())
			{
				return agentType.empty() ? task : task + " (" + agentType + ")";
			}
		}

		for (const char* inputKey : {"command", "file_path", "pattern", "description", "url", "query", "skill"})
		{
			if (std::string value = StringAt(input, inputKey); !value.empty())
			{
				return value;
			}
		}

		return TruncateUtf8(tool.InputJson, kRawSummaryBytes);
	}

	std::vector<std::string> EditLines(const ToolCall& tool)
	{
		std::vector<std::string> lines;

		if (tool.Name != "Edit")
		{
			return lines;
		}

		const nlohmann::json input = ObjectOf(tool.InputJson);
		AddLines(lines, StringAt(input, "old_string"), kRemovedLinePrefix);
		AddLines(lines, StringAt(input, "new_string"), kAddedLinePrefix);
		return lines;
	}

	std::optional<std::filesystem::path> ToolFilePath(const ToolCall& tool)
	{
		const std::string path = StringAt(ObjectOf(tool.InputJson), "file_path");

		if (path.empty())
		{
			return std::nullopt;
		}

		return Core::FromUtf8(path);
	}

	std::vector<TurnCost> TurnCosts(const Session& session, const Pricing& pricing)
	{
		std::vector<TurnCost> turnCosts;
		turnCosts.reserve(session.GetTurns().size());

		for (const Turn& turn : session.GetTurns())
		{
			TurnCost turnCost;

			for (const auto& [usageKey, counts] : turn.Usage)
			{
				if (const std::optional<BucketCosts> bucketCosts = pricing.BucketsOf(usageKey, counts))
				{
					turnCost.Buckets.Input += bucketCosts->Input;
					turnCost.Buckets.Output += bucketCosts->Output;
					turnCost.Buckets.CacheWrite += bucketCosts->CacheWrite;
					turnCost.Buckets.CacheRead += bucketCosts->CacheRead;
				}
				else
				{
					turnCost.Known = false;
				}
			}

			turnCosts.push_back(turnCost);
		}

		return turnCosts;
	}

	TurnBreakdown BreakDownTurn(const Session& session, std::size_t turnIndex, const RequestTimeline& timeline,
								const Pricing& pricing)
	{
		TurnBreakdown breakdown;

		if (turnIndex >= session.GetTurns().size())
		{
			return breakdown;
		}

		const Turn& turn = session.GetTurns()[turnIndex];

		for (const auto& [requestKey, request] : turn.Requests)
		{
			breakdown.Requests.push_back(
				{.Key = requestKey,
				 .Position = timeline.IndexOf(requestKey),
				 .Cost = pricing.CostOf(UsageKey{request.Model, request.Fast}, request.Usage)});
		}

		std::ranges::stable_sort(breakdown.Requests, IsDearerRequestFirst);

		std::map<std::string, std::size_t> callsByToolName;
		for (const TurnItem& item : turn.Items)
		{
			if (item.Kind == ItemKind::Tool && item.Tool < session.GetTools().size())
			{
				++callsByToolName[session.GetTools()[item.Tool].Name];
			}
		}

		for (const auto& [toolName, callCount] : callsByToolName)
		{
			breakdown.Tools.push_back({.Name = toolName, .Count = callCount});
		}

		std::ranges::stable_sort(breakdown.Tools, IsMoreUsedToolFirst);

		return breakdown;
	}

	std::vector<std::string> RequestActivity(const ApiRequest& request)
	{
		std::vector<std::string> activity;

		for (const ResponseItem& item : request.Response)
		{
			if (item.Kind == ResponseKind::ToolCall)
			{
				activity.push_back(SingleLineLabel(item.Label, kActivityBytes));
			}
		}

		if (!activity.empty())
		{
			return activity;
		}

		for (const ResponseItem& item : request.Response)
		{
			if (item.Kind == ResponseKind::Text && !item.Text.empty())
			{
				activity.push_back(std::format("{}{}{}", kLeftDoubleQuotationMark,
											   SingleLineLabel(item.Text, kActivityBytes),
											   kRightDoubleQuotationMark));
				break;
			}
		}

		return activity;
	}

	std::string SingleLineLabel(std::string_view text, std::size_t maxBytes)
	{
		std::string label;
		label.reserve(text.size());
		constexpr bool kNoSpacePending = false;
		bool isSpacePending = kNoSpacePending;

		for (const char character : text)
		{
			if (Core::IsAsciiWhitespace(character))
			{
				isSpacePending = !label.empty();
				continue;
			}

			if (std::exchange(isSpacePending, kNoSpacePending))
			{
				label += ' ';
			}

			// WARNING: ImGui takes "##" as the start of a widget's id and hides the rest of the label behind
			// it, so a space goes after every '#' that follows another: "###" becomes "# # #".
			if (character == '#' && !label.empty() && label.back() == '#')
			{
				label += ' ';
			}

			label += character;
		}

		return TruncateUtf8(label, maxBytes);
	}
}
