#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Session.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

	struct ToolFacts
	{
		ToolCategory Category = ToolCategory::Other;
		std::string Size;
		std::string Detail;
	};

	ToolFacts FactsOf(const ToolCall& tool, const SubagentUsage& subagents, const Pricing& pricing);

	// A view's facts of each tool call of a session, worked out once: a subagent's are worked out again when its usage
	// is followed further or the prices it is costed with change.
	class ToolFactsCache
	{
	public:
		// Every fact is dropped, for a session that grew or started again.
		void Reset(std::size_t toolCount);

		const ToolFacts& FactsOf(std::size_t toolIndex, const ToolCall& tool, const SubagentUsage& subagents,
								 std::uint64_t subagentsVersion, const Pricing& pricing);

	private:
		std::vector<std::optional<ToolFacts>> m_Facts;
		std::uint64_t m_SubagentsVersion = 0;
		std::map<std::string, Price> m_PricedWith;
	};

	std::string ImageText(std::string_view mediaType, std::uint64_t bytes);

	int CachedPercent(std::uint64_t cacheRead, std::uint64_t context);
	std::string FormatTokens(std::uint64_t tokens);

	std::string FormatTokensExact(std::uint64_t tokens);

	std::string UsageLine(const UsageByModel& usage, std::size_t requests, const Pricing& pricing);
}
