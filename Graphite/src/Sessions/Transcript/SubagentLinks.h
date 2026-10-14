#pragma once

#include <filesystem>
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
}
