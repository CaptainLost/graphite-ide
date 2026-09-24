#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Session.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Graphite {

	// What kind of work a tool does, which is what the conversation view colours it by.
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

	// Each subagent's usage, by the agent id its Agent call's result names.
	using SubagentUsage = std::unordered_map<std::string, UsageByModel>;

	// Reads <session>/subagents/agent-<id>.jsonl, each counted once per request.
	SubagentUsage LoadSubagentUsage(const std::filesystem::path& sessionFile);

	struct ToolFacts
	{
		ToolCategory Category = ToolCategory::Other;

		// How much came back: lines read, bytes of output. Empty when nothing says.
		std::string Size;

		// The one fact particular to the tool: +/- lines, matches, interrupted, subagent.
		std::string Detail;
	};

	ToolFacts FactsOf(const ToolCall& tool, const SubagentUsage& subagents, const Pricing& pricing);

	std::string FormatBytes(std::uint64_t bytes);

	// An image in the text a request sent, "[image/png, 3.0 KB]": what it is and how big, since
	// its pixels are not text. Whatever is not known is left out, down to "[image]".
	std::string ImageText(std::string_view mediaType, std::uint64_t bytes);

	// The share of `context` read from the cache, as a whole percent cut rather than rounded:
	// 99.9% is not all of it. Zero for no context.
	int CachedPercent(std::uint64_t cacheRead, std::uint64_t context);
	std::string FormatTokens(std::uint64_t tokens);

	// "opus-5 · 201k in (99% cache) · 310 out · $0.12", prefixed "N requests · " when there
	// were several. Several models are named together, joined by "+".
	std::string UsageLine(const UsageByModel& usage, std::size_t requests, const Pricing& pricing);
}
