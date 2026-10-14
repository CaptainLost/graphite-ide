#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Session.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Graphite {

	enum class ToolCategory
	{
		Read,
		Edit,
		Run,
		Agent,
		Web,
		Other
	};

	ToolCategory CategoryOf(std::string_view toolName);

	bool IsAgentCall(const ToolCall& tool);

	using SubagentUsage = std::unordered_map<std::string, UsageByModel>;

	SubagentUsage LoadSubagentUsage(const std::filesystem::path& sessionFile);

	struct ToolFacts
	{
		ToolCategory Category = ToolCategory::Other;
		std::string Size;
		std::string Detail;
	};

	ToolFacts FactsOf(const ToolCall& tool, const SubagentUsage& subagents, const Pricing& pricing);

	std::string ImageText(std::string_view mediaType, std::uint64_t bytes);

	int CachedPercent(std::uint64_t cacheRead, std::uint64_t context);
	std::string FormatTokens(std::uint64_t tokens);

	std::string FormatTokensExact(std::uint64_t tokens);

	std::string UsageLine(const UsageByModel& usage, std::size_t requests, const Pricing& pricing);
}
