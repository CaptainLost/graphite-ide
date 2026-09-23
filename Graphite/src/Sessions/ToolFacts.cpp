#include "Sessions/ToolFacts.h"

#include "Sessions/SessionSummary.h"
#include "Sessions/SessionText.h"
#include "Sessions/TranscriptReader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>

namespace Graphite {

	namespace {
		const char* const kDot = " \xc2\xb7 ";

		nlohmann::json ObjectOf(const std::string& json)
		{
			nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);
			return parsed.is_object() ? parsed : nlohmann::json::object();
		}

		std::optional<std::uint64_t> CountAt(const nlohmann::json& object, const char* key)
		{
			const auto found = object.find(key);
			if (found == object.end() || !found->is_number_integer() || found->get<std::int64_t>() < 0)
				return std::nullopt;
			return static_cast<std::uint64_t>(found->get<std::int64_t>());
		}

		std::string StringAt(const nlohmann::json& object, const char* key)
		{
			const auto found = object.find(key);
			return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
		}

		std::string ShortModel(std::string_view model)
		{
			return std::string{model.starts_with("claude-") ? model.substr(7) : model};
		}

		std::string LinesText(std::uint64_t lines)
		{
			return std::to_string(lines) + (lines == 1 ? " line" : " lines");
		}

		std::string PatchDetail(const nlohmann::json& result)
		{
			const auto patch = result.find("structuredPatch");
			if (patch == result.end() || !patch->is_array())
				return {};

			if (patch->empty() && StringAt(result, "type") == "create")
			{
				const std::string content = StringAt(result, "content");
				const std::uint64_t lines = content.empty() ? 0 : 1 + std::count(content.begin(), content.end(), '\n');
				return "new file, " + LinesText(lines);
			}

			std::uint64_t added = 0;
			std::uint64_t removed = 0;

			for (const nlohmann::json& hunk : *patch)
			{
				const auto lines = hunk.is_object() ? hunk.find("lines") : hunk.end();
				if (!hunk.is_object() || lines == hunk.end() || !lines->is_array())
					continue;

				for (const nlohmann::json& line : *lines)
				{
					if (!line.is_string() || line.get_ref<const std::string&>().empty())
						continue;
					const char mark = line.get_ref<const std::string&>().front();
					added += mark == '+';
					removed += mark == '-';
				}
			}

			return "+" + std::to_string(added) + " \xe2\x88\x92" + std::to_string(removed);
		}
	}

	ToolCategory CategoryOf(std::string_view name)
	{
		for (const std::string_view read : {"Read", "Grep", "Glob", "NotebookRead", "ToolSearch"})
			if (name == read)
				return ToolCategory::Read;
		for (const std::string_view edit : {"Edit", "Write", "NotebookEdit", "MultiEdit"})
			if (name == edit)
				return ToolCategory::Edit;
		for (const std::string_view run : {"Bash", "PowerShell", "Monitor"})
			if (name == run)
				return ToolCategory::Run;
		for (const std::string_view agent : {"Agent", "Task", "SendMessage", "Workflow", "Skill"})
			if (name == agent)
				return ToolCategory::Agent;
		for (const std::string_view web : {"WebFetch", "WebSearch"})
			if (name == web)
				return ToolCategory::Web;

		return ToolCategory::Other;
	}

	SubagentUsage LoadSubagentUsage(const std::filesystem::path& sessionFile)
	{
		SubagentUsage usage;
		std::error_code status;

		for (std::filesystem::directory_iterator it{SubagentDirectory(sessionFile), status}, end;
			 !status && it != end; it.increment(status))
		{
			const std::string stem = it->path().stem().string();

			if (it->path().extension() != ".jsonl" || !stem.starts_with("agent-"))
				continue;

			UsageTally tally;

			if (Core::Result<TranscriptRead> read = ReadTranscript(it->path(), 0))
			{
				for (const TranscriptRecord& record : read.GetValue().Chunk.Records)
					tally.Add(record);
			}

			usage[stem.substr(6)] = tally.Get();
		}

		return usage;
	}

	std::string FormatBytes(std::uint64_t bytes)
	{
		char buffer[32];

		if (bytes < 1024)
			std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
		else if (bytes < 1024 * 1024)
			std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
		else
			std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));

		return buffer;
	}

	std::string FormatTokens(std::uint64_t tokens)
	{
		char buffer[32];

		if (tokens < 1000)
			std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(tokens));
		else if (tokens < 1'000'000)
			std::snprintf(buffer, sizeof(buffer), "%.1fk", static_cast<double>(tokens) / 1000.0);
		else
			std::snprintf(buffer, sizeof(buffer), "%.1fM", static_cast<double>(tokens) / 1'000'000.0);

		return buffer;
	}

	ToolFacts FactsOf(const ToolCall& tool, const SubagentUsage& subagents, const Pricing& pricing)
	{
		ToolFacts facts;
		facts.Category = CategoryOf(tool.Name);

		const nlohmann::json result = ObjectOf(tool.ResultJson);

		if (tool.Name == "Read")
		{
			if (const auto file = result.find("file"); file != result.end() && file->is_object())
				if (const auto lines = CountAt(*file, "numLines"))
					facts.Size = LinesText(*lines);
		}
		else if (tool.Name == "Bash" || tool.Name == "PowerShell")
		{
			// Output that was too large to keep inline is stored apart; its size says how big
			// it really was, where stdout holds only the head.
			if (const auto stored = CountAt(result, "persistedOutputSize"))
				facts.Size = FormatBytes(*stored);
			else if (result.contains("stdout") || result.contains("stderr"))
				facts.Size = FormatBytes(StringAt(result, "stdout").size() + StringAt(result, "stderr").size());

			if (const auto interrupted = result.find("interrupted");
				interrupted != result.end() && interrupted->is_boolean() && interrupted->get<bool>())
				facts.Detail = "interrupted";
		}
		else if (tool.Name == "Edit" || tool.Name == "Write" || tool.Name == "MultiEdit")
		{
			facts.Detail = PatchDetail(result);
		}
		else if (tool.Name == "Grep")
		{
			const auto files = CountAt(result, "numFiles");
			const auto lines = CountAt(result, "numLines");

			if (StringAt(result, "mode") == "content" && lines && files)
				facts.Detail = std::to_string(*lines) + " matches in " + std::to_string(*files) + " files";
			else if (files)
				facts.Detail = std::to_string(*files) + " files";
		}
		else if (tool.Name == "Agent" || tool.Name == "Task")
		{
			const nlohmann::json input = ObjectOf(tool.InputJson);
			std::string detail = StringAt(input, "subagent_type");

			if (const std::string model = StringAt(result, "resolvedModel"); !model.empty())
				detail += (detail.empty() ? "" : kDot) + ShortModel(model);

			// No transcript yet — the subagent is still starting — means no cost, not $0.00.
			if (const auto found = subagents.find(StringAt(result, "agentId")); found != subagents.end())
				detail += (detail.empty() ? "" : kDot) + FormatCost(pricing.CostOf(found->second));

			facts.Detail = detail;
		}
		else if (tool.Name == "WebFetch")
		{
			if (const auto bytes = CountAt(result, "bytes"))
				facts.Size = FormatBytes(*bytes);
			if (const auto code = CountAt(result, "code"))
				facts.Detail = "HTTP " + std::to_string(*code);
		}

		if (facts.Size.empty() && tool.ResultBytes > 0 && tool.Name != "Agent" && tool.Name != "Task")
			facts.Size = FormatBytes(tool.ResultBytes);

		return facts;
	}

	std::string UsageLine(const UsageByModel& usage, std::size_t requests, const Pricing& pricing)
	{
		TokenCounts total;
		std::string models;

		for (const auto& [key, counts] : usage)
		{
			total += counts;

			const std::string name = ShortModel(key.Model) + (key.Fast ? " fast" : "");
			if (models.find(name) == std::string::npos)
				models += (models.empty() ? "" : "+") + name;
		}

		const std::uint64_t input = total.Input + total.CacheWrite5m + total.CacheWrite1h + total.CacheRead;
		const int cached = input == 0 ? 0 : static_cast<int>(100.0 * static_cast<double>(total.CacheRead) / static_cast<double>(input));

		std::string line = std::to_string(requests) + (requests == 1 ? " request" : " requests") + kDot;
		line += models + kDot + FormatTokens(input) + " in (" + std::to_string(cached) + "% cache)" + kDot +
				FormatTokens(total.Output) + " out" + kDot + FormatCost(pricing.CostOf(usage));
		return line;
	}
}
