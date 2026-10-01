#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Graphite {

	// What a subagent's meta file says about it: which agent it is, of what type, and the
	// description the call that started it gave.
	struct SubagentInfo
	{
		std::string AgentId;
		std::string AgentType;
		std::string Description;

		friend bool operator==(const SubagentInfo&, const SubagentInfo&) = default;
	};

	// The subagents a session started, by the id of the Agent call that started each.
	using SubagentLinks = std::unordered_map<std::string, SubagentInfo>;

	// Reads `agent-<id>.meta.json`: the id of the call that started agent `agentId`, and what
	// the file says about it. Nothing when the text names no call - malformed, not an object,
	// or `toolUseId` missing, empty or not a string.
	std::optional<std::pair<std::string, SubagentInfo>> ParseSubagentMeta(std::string_view json, std::string agentId);

	// ParseSubagentMeta over the meta file beside `transcript` (`agent-<id>.jsonl`). Nothing when
	// that file is missing or unreadable.
	std::optional<std::pair<std::string, SubagentInfo>> ReadSubagentMeta(const std::filesystem::path& transcript);

	// `<session>/subagents/agent-<id>.jsonl`.
	std::filesystem::path SubagentTranscriptPath(const std::filesystem::path& sessionFile, std::string_view agentId);
}
