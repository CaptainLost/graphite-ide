#include "Sessions/Transcript/TurnParts.h"

#include "Sessions/SessionSummary.h"

#include <algorithm>
#include <map>
#include <set>

namespace Graphite {

	namespace {
		using Timestamp = std::chrono::sys_time<std::chrono::milliseconds>;
		using ToolCount = std::pair<std::string, std::size_t>;
		using RequestPartIndices = std::map<std::string, std::size_t>;

		bool IsVisible(const TurnItem& item)
		{
			return !(item.Kind == ItemKind::Thinking && item.Text.empty());
		}

		PartKind PartKindOf(const TurnItem& item)
		{
			switch (item.Kind)
			{
			case ItemKind::Text:
				return PartKind::Reply;
			case ItemKind::Injected:
				return PartKind::Injected;
			case ItemKind::Queued:
				return PartKind::Queued;
			case ItemKind::Compaction:
				return PartKind::Compaction;
			default:
				return PartKind::Steps;
			}
		}

		ToolCount* FindToolCount(std::vector<ToolCount>& toolCounts, const std::string& toolName)
		{
			for (ToolCount& toolCount : toolCounts)
			{
				const auto& [countedToolName, timesUsed] = toolCount;
				if (countedToolName == toolName)
				{
					return &toolCount;
				}
			}
			return nullptr;
		}

		bool IsUsedMoreOften(const ToolCount& candidate, const ToolCount& other)
		{
			const auto& [candidateName, candidateTimesUsed] = candidate;
			const auto& [otherName, otherTimesUsed] = other;
			return candidateTimesUsed > otherTimesUsed;
		}

		void SortMostUsedFirstKeepingFirstUseOrder(std::vector<ToolCount>& toolCounts)
		{
			std::stable_sort(toolCounts.begin(), toolCounts.end(), IsUsedMoreOften);
		}

		void SummariseToolCalls(TurnPart& part, const Turn& turn, const std::vector<ToolCall>& tools)
		{
			std::vector<ToolCount> toolCounts;
			std::optional<Timestamp> earliestStart;
			std::optional<Timestamp> latestFinish;

			for (const std::size_t itemIndex : part.Items)
			{
				const TurnItem& item = turn.Items[itemIndex];
				if (item.Kind != ItemKind::Tool)
				{
					continue;
				}

				const ToolCall& tool = tools[item.Tool];

				ToolCount* existingCount = FindToolCount(toolCounts, tool.Name);
				if (existingCount == nullptr)
				{
					toolCounts.emplace_back(tool.Name, 1);
				}
				else
				{
					auto& [toolName, timesUsed] = *existingCount;
					++timesUsed;
				}

				if (tool.IsError)
				{
					++part.Failures;
				}

				const std::optional<Timestamp> start = ParseTimestamp(tool.StartedAt);
				if (start && (!earliestStart || *start < *earliestStart))
				{
					earliestStart = start;
				}
				const std::optional<Timestamp> finish = ParseTimestamp(tool.FinishedAt);
				if (finish && (!latestFinish || *finish > *latestFinish))
				{
					latestFinish = finish;
				}
			}

			SortMostUsedFirstKeepingFirstUseOrder(toolCounts);
			part.ToolCounts = std::move(toolCounts);

			if (earliestStart && latestFinish && *latestFinish > *earliestStart)
			{
				part.Duration = *latestFinish - *earliestStart;
			}
		}

		void RememberRequestInFirstSeenOrder(std::vector<std::string>& requestsInFirstSeenOrder,
											 std::set<std::string>& requestsSeen, const std::string& request)
		{
			const auto [position, wasInserted] = requestsSeen.insert(request);
			if (wasInserted)
			{
				requestsInFirstSeenOrder.push_back(request);
			}
		}

		void ConsiderTimestamp(std::string& latestText, std::optional<Timestamp>& latestTime,
							   const std::string& timestampText)
		{
			const std::optional<Timestamp> parsed = ParseTimestamp(timestampText);
			if (parsed && (!latestTime || *parsed > *latestTime))
			{
				latestTime = parsed;
				latestText = timestampText;
			}
		}
	}

	std::vector<TurnPart> SplitTurn(const Turn& turn, const std::vector<ToolCall>& tools)
	{
		std::vector<TurnPart> parts;

		RequestPartIndices partOfRequest;
		std::set<std::string> requestsAwaitingVisibleItem;

		std::vector<std::string> requestsInFirstSeenOrder;
		std::set<std::string> requestsSeen;

		for (std::size_t itemIndex = 0; itemIndex < turn.Items.size(); ++itemIndex)
		{
			const TurnItem& item = turn.Items[itemIndex];

			if (!IsVisible(item))
			{
				if (!item.RequestId.empty())
				{
					requestsAwaitingVisibleItem.insert(item.RequestId);
					RememberRequestInFirstSeenOrder(requestsInFirstSeenOrder, requestsSeen, item.RequestId);
				}
				continue;
			}

			const PartKind kind = PartKindOf(item);

			const bool continuesStepRun =
				!parts.empty() && kind == PartKind::Steps && parts.back().Kind == PartKind::Steps;
			if (!continuesStepRun)
			{
				parts.push_back({.Kind = kind});
			}

			parts.back().Items.push_back(itemIndex);

			const std::size_t partIndex = parts.size() - 1;
			if (!item.RequestId.empty())
			{
				partOfRequest[item.RequestId] = partIndex;
				RememberRequestInFirstSeenOrder(requestsInFirstSeenOrder, requestsSeen, item.RequestId);
			}
			for (const std::string& request : requestsAwaitingVisibleItem)
			{
				partOfRequest[request] = partIndex;
			}
			requestsAwaitingVisibleItem.clear();
		}

		if (!parts.empty())
		{
			for (const std::string& request : requestsAwaitingVisibleItem)
			{
				partOfRequest[request] = parts.size() - 1;
			}
		}

		for (const auto& [request, partIndex] : partOfRequest)
		{
			const std::map<std::string, RequestUsage>::const_iterator requestUsage = turn.Requests.find(request);
			if (requestUsage == turn.Requests.end())
			{
				continue;
			}

			const auto& [requestKey, usage] = *requestUsage;
			parts[partIndex].Usage[{usage.Model, usage.Fast}] += usage.Usage;
			++parts[partIndex].Requests;
		}

		for (const std::string& request : requestsInFirstSeenOrder)
		{
			const RequestPartIndices::const_iterator owner = partOfRequest.find(request);
			if (owner != partOfRequest.end() && turn.Requests.contains(request))
			{
				const auto& [ownedRequest, ownerPartIndex] = *owner;
				parts[ownerPartIndex].RequestKeys.push_back(request);
			}
		}

		for (TurnPart& part : parts)
		{
			if (part.Kind == PartKind::Steps)
			{
				SummariseToolCalls(part, turn, tools);
			}
		}

		return parts;
	}

	std::string LastActivity(const Turn& turn, const TurnPart& part, const std::vector<ToolCall>& tools)
	{
		std::string latestText;
		std::optional<Timestamp> latestTime;

		for (const std::size_t itemIndex : part.Items)
		{
			const TurnItem& item = turn.Items[itemIndex];
			ConsiderTimestamp(latestText, latestTime, item.At);

			if (item.Kind == ItemKind::Tool)
			{
				ConsiderTimestamp(latestText, latestTime, tools[item.Tool].FinishedAt);
			}
		}

		return latestText;
	}

	std::optional<std::chrono::milliseconds> GapBetween(std::string_view earlier, std::string_view later)
	{
		const std::optional<Timestamp> earlierTime = ParseTimestamp(earlier);
		const std::optional<Timestamp> laterTime = ParseTimestamp(later);

		if (!earlierTime || !laterTime || *laterTime - *earlierTime <= kGapThreshold)
		{
			return std::nullopt;
		}

		return *laterTime - *earlierTime;
	}

	std::optional<std::chrono::milliseconds> GapBefore(std::string_view earlier, const TurnItem& item)
	{
		const std::optional<Timestamp> earlierTime = ParseTimestamp(earlier);
		const std::optional<Timestamp> itemTime = ParseTimestamp(item.At);

		if (!earlierTime || !itemTime)
		{
			return std::nullopt;
		}

		std::chrono::milliseconds span = *itemTime - *earlierTime;

		if (item.Kind == ItemKind::Compaction && item.Compaction.Duration)
		{
			span -= *item.Compaction.Duration;
		}

		if (span <= kGapThreshold)
		{
			return std::nullopt;
		}

		return span;
	}
}
