#include "Sessions/SessionText.h"

#include "Sessions/SessionSummary.h"

#include <nlohmann/json.hpp>

#include <cstdio>

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

	std::vector<BucketCosts> TurnCosts(const Session& session, const Pricing& pricing)
	{
		std::vector<BucketCosts> costs;
		costs.reserve(session.GetTurns().size());

		for (const Turn& turn : session.GetTurns())
		{
			BucketCosts sum;

			for (const auto& [key, counts] : turn.Usage)
			{
				if (const std::optional<BucketCosts> part = pricing.BucketsOf(key, counts))
				{
					sum.Input += part->Input;
					sum.Output += part->Output;
					sum.CacheWrite += part->CacheWrite;
					sum.CacheRead += part->CacheRead;
				}
			}

			costs.push_back(sum);
		}

		return costs;
	}
}
