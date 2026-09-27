#include "Sessions/SessionText.h"

#include "Sessions/SessionSummary.h"
#include "Sessions/ToolFacts.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::size_t kRawSummaryBytes = 80;

		nlohmann::json InputOf(const ToolCall& tool)
		{
			nlohmann::json input = nlohmann::json::parse(tool.InputJson, nullptr, false);
			return input.is_object() ? input : nlohmann::json::object();
		}

		std::string Field(const nlohmann::json& input, const char* key)
		{
			const auto found = input.find(key);
			return found != input.end() && found->is_string() ? found->get<std::string>() : std::string{};
		}

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
			return "?";

		if (cost.Dollars > 0.0 && cost.Dollars < 0.005)
			return "< $0.01";

		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "$%.2f", cost.Dollars);
		return buffer;
	}

	std::string FormatDuration(std::chrono::milliseconds span)
	{
		const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(span).count();
		char buffer[32];

		if (seconds < 60)
			std::snprintf(buffer, sizeof(buffer), "%llds", static_cast<long long>(seconds));
		else if (seconds < 3600)
			std::snprintf(buffer, sizeof(buffer), "%lldm", static_cast<long long>(seconds / 60));
		else
			std::snprintf(buffer, sizeof(buffer), "%lldh %lldm", static_cast<long long>(seconds / 3600),
						  static_cast<long long>(seconds % 3600 / 60));

		return buffer;
	}

	std::string CompactionLabel(const CompactionFacts& facts, const std::string& time)
	{
		std::string label = "Context compacted";

		const auto add = [&label](const std::string& piece) { label += " \xc2\xb7 " + piece; };

		if (facts.PreTokens && facts.PostTokens)
			add(FormatTokens(*facts.PreTokens) + " \xe2\x86\x92 " + FormatTokens(*facts.PostTokens));
		if (!facts.Trigger.empty())
			add(facts.Trigger);
		if (facts.Duration)
			add(FormatDuration(*facts.Duration));
		if (!time.empty())
			add(time);

		return label;
	}

	std::string ToolSummary(const ToolCall& tool)
	{
		const nlohmann::json input = InputOf(tool);

		// A subagent is named by its task and the kind of agent given it.
		if (tool.Name == "Agent")
		{
			const std::string task = Field(input, "description");
			const std::string type = Field(input, "subagent_type");

			if (!task.empty())
				return type.empty() ? task : task + " (" + type + ")";
		}

		for (const char* key : {"command", "file_path", "pattern", "description", "url", "query", "skill"})
		{
			if (std::string value = Field(input, key); !value.empty())
				return value;
		}

		return TruncateUtf8(tool.InputJson, kRawSummaryBytes);
	}

	std::vector<std::string> EditLines(const ToolCall& tool)
	{
		std::vector<std::string> lines;

		if (tool.Name != "Edit")
			return lines;

		const nlohmann::json input = InputOf(tool);
		AddLines(lines, Field(input, "old_string"), "- ");
		AddLines(lines, Field(input, "new_string"), "+ ");
		return lines;
	}

	std::optional<std::filesystem::path> ToolFilePath(const ToolCall& tool)
	{
		const std::string path = Field(InputOf(tool), "file_path");

		if (path.empty())
			return std::nullopt;

		return std::filesystem::path{std::u8string{reinterpret_cast<const char8_t*>(path.data()), path.size()}};
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
			return breakdown;

		const Turn& at = session.GetTurns()[turn];

		for (const auto& [key, request] : at.Requests)
			breakdown.Requests.push_back({.Key = key,
										  .Position = timeline.IndexOf(key),
										  .Cost = pricing.CostOf({{UsageKey{request.Model, request.Fast}, request.Usage}})});

		// Stable, so requests that cost the same keep the order of their keys.
		std::ranges::stable_sort(breakdown.Requests, [](const TurnRequest& a, const TurnRequest& b) {
			if (a.Cost.IsKnown() != b.Cost.IsKnown())
				return a.Cost.IsKnown();
			return a.Cost.Dollars > b.Cost.Dollars;
		});

		std::map<std::string, std::size_t> counts;
		for (const TurnItem& item : at.Items)
			if (item.Kind == ItemKind::Tool && item.Tool < session.GetTools().size())
				++counts[session.GetTools()[item.Tool].Name];

		for (const auto& [name, count] : counts)
			breakdown.Tools.push_back({name, count});

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
			if (item.Kind == ResponseKind::ToolCall)
				activity.push_back(SingleLineLabel(item.Label, kActivityBytes));

		if (!activity.empty())
			return activity;

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
				label += ' ';

			// "###" becomes "# # #": a space after every '#' that follows another leaves no
			// two side by side.
			if (c == '#' && !label.empty() && label.back() == '#')
				label += ' ';

			label += c;
		}

		return TruncateUtf8(label, maxBytes);
	}
}
