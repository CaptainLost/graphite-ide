#include "Sessions/Transcript/ToolFacts.h"

#include "Core/Base/ByteSize.h"
#include "Core/Base/Strings.h"
#include "Core/FileSystem/FileSystem.h"

#include "Sessions/Transcript/JsonAccess.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/SessionText.h"
#include "Sessions/Transcript/TranscriptReader.h"
#include "UI/TypographicMarks.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>

namespace Graphite {

	namespace {
		constexpr std::string_view kClaudeModelPrefix = "claude-";
		constexpr std::string_view kAgentFilePrefix = "agent-";
		constexpr std::string_view kModelNamesSeparator = "+";
		constexpr std::uint64_t kTranscriptStart = 0;
		constexpr std::uint64_t kPercentScale = 100;
		constexpr std::uint64_t kTokensPerThousand = 1000;
		constexpr std::uint64_t kTokensPerMillion = 1'000'000;
		constexpr std::size_t kDigitsPerGroup = 3;

		std::string ShortModel(std::string_view model)
		{
			return std::string{model.starts_with(kClaudeModelPrefix) ? model.substr(kClaudeModelPrefix.size()) : model};
		}

		std::string LinesText(std::uint64_t lines)
		{
			return std::to_string(lines) + (lines == 1 ? " line" : " lines");
		}

		std::uint64_t LineCountOf(const std::string& content)
		{
			if (content.empty())
			{
				return 0;
			}

			const std::uint64_t newlineCount = std::count(content.begin(), content.end(), '\n');
			return 1 + newlineCount;
		}

		std::string PatchDetail(const nlohmann::json& result)
		{
			const nlohmann::json::const_iterator patch = result.find("structuredPatch");
			if (patch == result.end() || !patch->is_array())
			{
				return {};
			}

			if (patch->empty() && StringAt(result, "type") == "create")
			{
				const std::string content = StringAt(result, "content");
				return "new file, " + LinesText(LineCountOf(content));
			}

			std::uint64_t added = 0;
			std::uint64_t removed = 0;

			for (const nlohmann::json& hunk : *patch)
			{
				const nlohmann::json::const_iterator hunkLines = hunk.is_object() ? hunk.find("lines") : hunk.end();
				if (!hunk.is_object() || hunkLines == hunk.end() || !hunkLines->is_array())
				{
					continue;
				}

				for (const nlohmann::json& line : *hunkLines)
				{
					if (!line.is_string() || line.get_ref<const std::string&>().empty())
					{
						continue;
					}
					const char mark = line.get_ref<const std::string&>().front();
					added += mark == '+';
					removed += mark == '-';
				}
			}

			return std::format("+{} {}{}", added, kMinusSign, removed);
		}
	}

	ToolCategory CategoryOf(std::string_view toolName)
	{
		for (const std::string_view readTool : {"Read", "Grep", "Glob", "NotebookRead", "ToolSearch"})
		{
			if (toolName == readTool)
			{
				return ToolCategory::Read;
			}
		}
		for (const std::string_view editTool : {"Edit", "Write", "NotebookEdit", "MultiEdit"})
		{
			if (toolName == editTool)
			{
				return ToolCategory::Edit;
			}
		}
		for (const std::string_view runTool : {"Bash", "PowerShell", "Monitor"})
		{
			if (toolName == runTool)
			{
				return ToolCategory::Run;
			}
		}
		for (const std::string_view agentTool : {"Agent", "Task", "SendMessage", "Workflow", "Skill"})
		{
			if (toolName == agentTool)
			{
				return ToolCategory::Agent;
			}
		}
		for (const std::string_view webTool : {"WebFetch", "WebSearch"})
		{
			if (toolName == webTool)
			{
				return ToolCategory::Web;
			}
		}

		return ToolCategory::Other;
	}

	bool IsAgentCall(const ToolCall& tool)
	{
		return tool.Name == "Agent" || tool.Name == "Task";
	}

	SubagentUsage LoadSubagentUsage(const std::filesystem::path& sessionFile)
	{
		SubagentUsage usage;
		std::error_code directoryStatus;

		for (std::filesystem::directory_iterator entryIterator{SubagentDirectory(sessionFile), directoryStatus}, endOfDirectory;
			 !directoryStatus && entryIterator != endOfDirectory; entryIterator.increment(directoryStatus))
		{
			const std::string stem = Core::ToUtf8(entryIterator->path().stem());

			if (entryIterator->path().extension() != ".jsonl" || !stem.starts_with(kAgentFilePrefix))
			{
				continue;
			}

			UsageTally tally;

			if (Core::Result<TranscriptRead> read = ReadTranscript(entryIterator->path(), kTranscriptStart))
			{
				for (const TranscriptRecord& record : read.GetValue().Chunk.Records)
				{
					tally.Add(record);
				}
			}

			usage[stem.substr(kAgentFilePrefix.size())] = tally.Get();
		}

		return usage;
	}

	std::string ImageText(std::string_view mediaType, std::uint64_t bytes)
	{
		std::string text = "[" + (mediaType.empty() ? std::string{"image"} : std::string{mediaType});

		if (bytes > 0)
		{
			text += ", " + Core::FormatByteSize(bytes);
		}

		return text + "]";
	}

	int CachedPercent(std::uint64_t cacheRead, std::uint64_t context)
	{
		if (context == 0)
		{
			return 0;
		}

		return static_cast<int>(std::min<std::uint64_t>(cacheRead, context) * kPercentScale / context);
	}

	std::string FormatTokens(std::uint64_t tokens)
	{
		if (tokens < kTokensPerThousand)
		{
			return std::format("{}", tokens);
		}

		if (tokens < kTokensPerMillion)
		{
			return std::format("{:.1f}k", static_cast<double>(tokens) / static_cast<double>(kTokensPerThousand));
		}

		return std::format("{:.1f}M", static_cast<double>(tokens) / static_cast<double>(kTokensPerMillion));
	}

	std::string FormatTokensExact(std::uint64_t tokens)
	{
		const std::string digits = std::to_string(tokens);
		std::string grouped;

		for (std::size_t digitIndex = 0; digitIndex < digits.size(); ++digitIndex)
		{
			const std::size_t digitsFromRight = digits.size() - digitIndex;
			const bool startsGroup = digitIndex > 0 && digitsFromRight % kDigitsPerGroup == 0;
			if (startsGroup)
			{
				grouped += ',';
			}

			grouped += digits[digitIndex];
		}

		return grouped;
	}

	ToolFacts FactsOf(const ToolCall& tool, const SubagentUsage& subagents, const Pricing& pricing)
	{
		ToolFacts facts;
		facts.Category = CategoryOf(tool.Name);

		const nlohmann::json result = ObjectOf(tool.ResultJson);

		if (tool.Name == "Read")
		{
			const nlohmann::json::const_iterator file = result.find("file");
			if (file != result.end() && file->is_object())
			{
				if (const std::optional<std::uint64_t> lineCount = OptionalCountAt(*file, "numLines"))
				{
					facts.Size = LinesText(*lineCount);
				}
			}
		}
		else if (tool.Name == "Bash" || tool.Name == "PowerShell")
		{
			if (const std::optional<std::uint64_t> persistedOutputSize = OptionalCountAt(result, "persistedOutputSize"))
			{
				facts.Size = Core::FormatByteSize(*persistedOutputSize);
			}
			else if (result.contains("stdout") || result.contains("stderr"))
			{
				facts.Size = Core::FormatByteSize(StringAt(result, "stdout").size() + StringAt(result, "stderr").size());
			}

			const nlohmann::json::const_iterator interrupted = result.find("interrupted");
			if (interrupted != result.end() && interrupted->is_boolean() && interrupted->get<bool>())
			{
				facts.Detail = "interrupted";
			}
		}
		else if (tool.Name == "Edit" || tool.Name == "Write" || tool.Name == "MultiEdit")
		{
			facts.Detail = PatchDetail(result);
		}
		else if (tool.Name == "Grep")
		{
			const std::optional<std::uint64_t> fileCount = OptionalCountAt(result, "numFiles");
			const std::optional<std::uint64_t> lineCount = OptionalCountAt(result, "numLines");

			if (StringAt(result, "mode") == "content" && lineCount && fileCount)
			{
				facts.Detail = std::to_string(*lineCount) + " matches in " + std::to_string(*fileCount) + " files";
			}
			else if (fileCount)
			{
				facts.Detail = std::to_string(*fileCount) + " files";
			}
		}
		else if (IsAgentCall(tool))
		{
			const nlohmann::json input = ObjectOf(tool.InputJson);
			std::string detail = StringAt(input, "subagent_type");

			if (const std::string model = StringAt(result, "resolvedModel"); !model.empty())
			{
				Core::AppendSeparated(detail, kMiddleDotSeparator, ShortModel(model));
			}

			const SubagentUsage::const_iterator subagentUsage = subagents.find(StringAt(result, "agentId"));
			if (subagentUsage != subagents.end())
			{
				const auto& [agentId, usageByModel] = *subagentUsage;
				Core::AppendSeparated(detail, kMiddleDotSeparator, FormatCost(pricing.CostOf(usageByModel)));
			}

			facts.Detail = detail;
		}
		else if (tool.Name == "WebFetch")
		{
			if (const std::optional<std::uint64_t> byteCount = OptionalCountAt(result, "bytes"))
			{
				facts.Size = Core::FormatByteSize(*byteCount);
			}
			if (const std::optional<std::uint64_t> statusCode = OptionalCountAt(result, "code"))
			{
				facts.Detail = "HTTP " + std::to_string(*statusCode);
			}
		}

		if (facts.Size.empty() && tool.ResultBytes > 0 && !IsAgentCall(tool))
		{
			facts.Size = Core::FormatByteSize(tool.ResultBytes);
		}

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
			{
				Core::AppendSeparated(models, kModelNamesSeparator, name);
			}
		}

		const std::uint64_t inputTokens = total.Input + total.CacheWrite5m + total.CacheWrite1h + total.CacheRead;
		const int cachedPercent = CachedPercent(total.CacheRead, inputTokens);

		std::string line = std::to_string(requests) + (requests == 1 ? " request" : " requests");
		line += kMiddleDotSeparator;
		line += models;
		line += kMiddleDotSeparator;
		line += FormatTokens(inputTokens) + " in (" + std::to_string(cachedPercent) + "% cache)";
		line += kMiddleDotSeparator;
		line += FormatTokens(total.Output) + " out";
		line += kMiddleDotSeparator;
		line += FormatCost(pricing.CostOf(usage));
		return line;
	}
}
