#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Graphite {

	struct SubagentInfo
	{
		std::string AgentId;
		std::string AgentType;
		std::string Description;

		friend bool operator==(const SubagentInfo&, const SubagentInfo&) = default;
	};

	using SubagentLinks = std::unordered_map<std::string, SubagentInfo>;

	std::optional<std::pair<std::string, SubagentInfo>> ParseSubagentMeta(std::string_view json, std::string agentId);

	std::optional<std::pair<std::string, SubagentInfo>> ReadSubagentMeta(const std::filesystem::path& transcript);

	std::filesystem::path SubagentTranscriptPath(const std::filesystem::path& sessionFile, std::string_view agentId);

	// The id in "agent-<id>.jsonl". Any other name is no subagent's transcript, so it has none.
	std::optional<std::string> SubagentIdOf(const std::filesystem::path& transcript);

	// Each subagent transcript in the session's subagents directory, with its id, in no particular order. False when an
	// error cut the walk short; a session without the directory has no subagents, and that walk saw it whole.
	bool ForEachSubagentTranscript(
		const std::filesystem::path& sessionFile,
		const std::function<void(const std::filesystem::path& transcript, const std::string& agentId)>& visit);
}
