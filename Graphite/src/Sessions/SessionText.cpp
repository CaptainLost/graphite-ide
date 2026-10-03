#include "Sessions/SessionText.h"

#include "Sessions/JsonAccess.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/ToolFacts.h"

#include "Core/Fs/FileSystem.h"

#include <format>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::size_t kRawSummaryBytes = 80;

		void AddLines(std::vector<std::string>& out, const std::string& text, const char* prefix)
		{
			std::size_t start = 0;

			while (start <= text.size())
			{
				const std::size_t end = std::min(text.find('\n', start), text.size());
				out.push_back(prefix + text.substr(start, end - start));
				start = end + 1;
			}
		}
	}

	std::string FormatCost(const CostResult& cost)
	{
		if (!cost.IsKnown())
		{
			return "?";
		}

		if (cost.Dollars > 0.0 && cost.Dollars < 0.005)
		{
			return "< $0.01";
		}

		return std::format("${:.2f}", cost.Dollars);
	}

	std::string FormatDuration(std::chrono::milliseconds span)
	{
		const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(span).count();

		if (seconds < 60)
		{
			return std::format("{}s", seconds);
		}

		if (seconds < 3600)
		{
			return std::format("{}m", seconds / 60);
		}

		return std::format("{}h {}m", seconds / 3600, seconds % 3600 / 60);
	}

	std::string CompactionLabel(const CompactionFacts& facts, const std::string& time)
	{
		std::string label = "Context compacted";

		const auto add = [&label](const std::string& piece) { label += " \xc2\xb7 " + piece; };

		if (facts.PreTokens && facts.PostTokens)
		{
			add(FormatTokens(*facts.PreTokens) + " \xe2\x86\x92 " + FormatTokens(*facts.PostTokens));
		}
		if (!facts.Trigger.empty())
		{
			add(facts.Trigger);
		}
		if (facts.Duration)
		{
			add(FormatDuration(*facts.Duration));
		}
		if (!time.empty())
		{
			add(time);
		}

		return label;
	}

	std::string ToolSummary(const ToolCall& tool)
	{
		const nlohmann::json input = ObjectOf(tool.InputJson);

		// A subagent is named by its task and the kind of agent given it.
		if (tool.Name == "Agent")
		{
			const std::string task = StringAt(input, "description");
			const std::string type = StringAt(input, "subagent_type");

			if (!task.empty())
			{
				return type.empty() ? task : task + " (" + type + ")";
			}
		}

		for (const char* key : {"command", "file_path", "pattern", "description", "url", "query", "skill"})
		{
			if (std::string value = StringAt(input, key); !value.empty())
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
		AddLines(lines, StringAt(input, "old_string"), "- ");
		AddLines(lines, StringAt(input, "new_string"), "+ ");
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
		std::vector<TurnCost> costs;
		costs.reserve(session.GetTurns().size());

		for (const Turn& turn : session.GetTurns())
		{
			TurnCost cost;

			for (const auto& [key, counts] : turn.Usage)
			{
				if (const std::optional<BucketCosts> part = pricing.BucketsOf(key, counts))
				{
					cost.Buckets.Input += part->Input;
					cost.Buckets.Output += part->Output;
					cost.Buckets.CacheWrite += part->CacheWrite;
					cost.Buckets.CacheRead += part->CacheRead;
				}
				else
				{
					cost.Known = false;
				}
			}

			costs.push_back(cost);
		}

		return costs;
	}

	TurnBreakdown BreakDownTurn(const Session& session, std::size_t turn, const RequestTimeline& timeline,
								const Pricing& pricing)
	{
		TurnBreakdown breakdown;

		if (turn >= session.GetTurns().size())
		{
			return breakdown;
		}

		const Turn& at = session.GetTurns()[turn];

		for (const auto& [key, request] : at.Requests)
		{
			breakdown.Requests.push_back({.Key = key,
										  .Position = timeline.IndexOf(key),
										  .Cost = pricing.CostOf(UsageKey{request.Model, request.Fast}, request.Usage)});
		}

		// Stable, so requests that cost the same keep the order of their keys.
		std::ranges::stable_sort(breakdown.Requests, [](const TurnRequest& a, const TurnRequest& b) {
			if (a.Cost.IsKnown() != b.Cost.IsKnown())
			{
				return a.Cost.IsKnown();
			}
			return a.Cost.Dollars > b.Cost.Dollars;
		});

		std::map<std::string, std::size_t> counts;
		for (const TurnItem& item : at.Items)
		{
			if (item.Kind == ItemKind::Tool && item.Tool < session.GetTools().size())
			{
				++counts[session.GetTools()[item.Tool].Name];
			}
		}

		for (const auto& [name, count] : counts)
		{
			breakdown.Tools.push_back({name, count});
		}

		// Stable over the map's order, so tools used as often read by name.
		std::ranges::stable_sort(breakdown.Tools,
								 [](const ToolCount& a, const ToolCount& b) { return a.Count > b.Count; });

		return breakdown;
	}

	std::vector<std::string> RequestActivity(const ApiRequest& request)
	{
		// Long enough for any path a row has room for; the row cuts it to its width.
		constexpr std::size_t kActivityBytes = 300;

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
				activity.push_back("\xe2\x80\x9c" + SingleLineLabel(item.Text, kActivityBytes) + "\xe2\x80\x9d");
				break;
			}
		}

		return activity;
	}

	std::string SingleLineLabel(std::string_view text, std::size_t maxBytes)
	{
		std::string label;
		label.reserve(text.size());
		bool space = false;

		for (const char c : text)
		{
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f')
			{
				space = !label.empty();
				continue;
			}

			if (std::exchange(space, false))
			{
				label += ' ';
			}

			// "###" becomes "# # #": a space after every '#' that follows another leaves no
			// two side by side.
			if (c == '#' && !label.empty() && label.back() == '#')
			{
				label += ' ';
			}

			label += c;
		}

		return TruncateUtf8(label, maxBytes);
	}
}
