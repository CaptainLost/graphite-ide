#include "Sessions/TurnParts.h"

#include "Sessions/SessionSummary.h"

#include <algorithm>
#include <map>
#include <set>

namespace Graphite {

	namespace {
		bool IsVisible(const TurnItem& item)
		{
			return !(item.Kind == ItemKind::Thinking && item.Text.empty());
		}

		PartKind KindOf(const TurnItem& item)
		{
			switch (item.Kind)
			{
			case ItemKind::Text: return PartKind::Reply;
			case ItemKind::Injected: return PartKind::Injected;
			case ItemKind::Queued: return PartKind::Queued;
			case ItemKind::Compaction: return PartKind::Compaction;
			default: return PartKind::Steps;
			}
		}

		void Summarise(TurnPart& part, const Turn& turn, const std::vector<ToolCall>& tools)
		{
			std::vector<std::pair<std::string, std::size_t>> counts;
			std::optional<std::chrono::sys_time<std::chrono::milliseconds>> first;
			std::optional<std::chrono::sys_time<std::chrono::milliseconds>> last;

			for (const std::size_t index : part.Items)
			{
				const TurnItem& item = turn.Items[index];
				if (item.Kind != ItemKind::Tool)
					continue;

				const ToolCall& tool = tools[item.Tool];

				const auto found = std::find_if(counts.begin(), counts.end(),
												[&tool](const auto& entry) { return entry.first == tool.Name; });
				if (found == counts.end())
					counts.emplace_back(tool.Name, 1);
				else
					++found->second;

				if (tool.IsError)
					++part.Failures;

				if (const auto start = ParseTimestamp(tool.StartedAt); start && (!first || *start < *first))
					first = start;
				if (const auto end = ParseTimestamp(tool.FinishedAt); end && (!last || *end > *last))
					last = end;
			}

			// Stable, so ties keep the order the tools were first used in.
			std::stable_sort(counts.begin(), counts.end(),
							 [](const auto& a, const auto& b) { return a.second > b.second; });
			part.ToolCounts = std::move(counts);

			if (first && last && *last > *first)
				part.Duration = *last - *first;
		}
	}

	std::vector<TurnPart> SplitTurn(const Turn& turn, const std::vector<ToolCall>& tools)
	{
		std::vector<TurnPart> parts;

		// Which part shows each request's usage: the part holding its last visible item.
		// Requests whose items were all dropped (empty thinking only) go with the next visible
		// item, or, at the end of the turn, with the last part.
		std::map<std::string, std::size_t> owner;
		std::set<std::string> pending;

		// Every request key seen, in first-seen order, so RequestKeys can be reported that way
		// once each key's final owner is known — owner itself, a std::map, sorts by key.
		std::vector<std::string> order;
		std::set<std::string> seen;

		const auto note = [&order, &seen](const std::string& request)
		{
			if (seen.insert(request).second)
				order.push_back(request);
		};

		for (std::size_t index = 0; index < turn.Items.size(); ++index)
		{
			const TurnItem& item = turn.Items[index];

			if (!IsVisible(item))
			{
				if (!item.RequestId.empty())
				{
					pending.insert(item.RequestId);
					note(item.RequestId);
				}
				continue;
			}

			const PartKind kind = KindOf(item);

			// Only step runs gather; a reply, an injected row, a queued message and a compaction are one item each.
			if (parts.empty() || kind != PartKind::Steps || parts.back().Kind != PartKind::Steps)
				parts.push_back({.Kind = kind});

			parts.back().Items.push_back(index);

			const std::size_t part = parts.size() - 1;
			if (!item.RequestId.empty())
			{
				owner[item.RequestId] = part;
				note(item.RequestId);
			}
			for (const std::string& request : pending)
				owner[request] = part;
			pending.clear();
		}

		if (!parts.empty())
		{
			for (const std::string& request : pending)
				owner[request] = parts.size() - 1;
		}

		for (const auto& [request, part] : owner)
		{
			const auto found = turn.Requests.find(request);
			if (found == turn.Requests.end())
				continue;

			parts[part].Usage[{found->second.Model, found->second.Fast}] += found->second.Usage;
			++parts[part].Requests;
		}

		// Only the keys counted above: a response written without usage is an item of the turn
		// but not one of its requests, and listing it would promise a row the count leaves out.
		for (const std::string& request : order)
		{
			const auto found = owner.find(request);
			if (found != owner.end() && turn.Requests.contains(request))
				parts[found->second].RequestKeys.push_back(request);
		}

		for (TurnPart& part : parts)
		{
			if (part.Kind == PartKind::Steps)
				Summarise(part, turn, tools);
		}

		return parts;
	}

	std::string LastActivity(const Turn& turn, const TurnPart& part, const std::vector<ToolCall>& tools)
	{
		std::string latest;
		std::optional<std::chrono::sys_time<std::chrono::milliseconds>> latestTime;

		const auto consider = [&latest, &latestTime](const std::string& timestamp)
		{
			if (const auto parsed = ParseTimestamp(timestamp); parsed && (!latestTime || *parsed > *latestTime))
			{
				latestTime = parsed;
				latest = timestamp;
			}
		};

		for (const std::size_t index : part.Items)
		{
			const TurnItem& item = turn.Items[index];
			consider(item.At);

			if (item.Kind == ItemKind::Tool)
				consider(tools[item.Tool].FinishedAt);
		}

		return latest;
	}

	std::optional<std::chrono::milliseconds> GapBetween(std::string_view earlier, std::string_view later)
	{
		const auto from = ParseTimestamp(earlier);
		const auto to = ParseTimestamp(later);

		if (!from || !to || *to - *from <= kGapThreshold)
			return std::nullopt;

		return *to - *from;
	}

	std::optional<std::chrono::milliseconds> GapBefore(std::string_view earlier, const TurnItem& item)
	{
		const auto from = ParseTimestamp(earlier);
		const auto to = ParseTimestamp(item.At);

		if (!from || !to)
			return std::nullopt;

		std::chrono::milliseconds span = *to - *from;

		if (item.Kind == ItemKind::Compaction && item.Compaction.Duration)
			span -= *item.Compaction.Duration;

		if (span <= kGapThreshold)
			return std::nullopt;

		return span;
	}
}
