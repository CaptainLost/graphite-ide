#include "Sessions/SubagentLinks.h"

#include "Core/Fs/FileSystem.h"

#include "Sessions/JsonAccess.h"
#include "Sessions/SessionSummary.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>

namespace Graphite {

	namespace {
		constexpr std::string_view kAgentPrefix = "agent-";
	}

	std::optional<std::pair<std::string, SubagentInfo>> ParseSubagentMeta(std::string_view json, std::string agentId)
	{
		const nlohmann::json parsed = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
		if (!parsed.is_object())
		{
			return std::nullopt;
		}

		std::string toolUseId = StringAt(parsed, "toolUseId");
		if (toolUseId.empty())
		{
			return std::nullopt;
		}

		SubagentInfo info;
		info.AgentId = std::move(agentId);
		info.AgentType = StringAt(parsed, "agentType");
		info.Description = StringAt(parsed, "description");

		return std::pair{std::move(toolUseId), std::move(info)};
	}

	std::optional<std::pair<std::string, SubagentInfo>> ReadSubagentMeta(const std::filesystem::path& transcript)
	{
		const std::string stem = Core::ToUtf8(transcript.stem());
		if (!stem.starts_with(kAgentPrefix))
		{
			return std::nullopt;
		}

		std::filesystem::path meta = transcript;
		meta.replace_extension(".meta.json");

		std::ifstream stream{meta, std::ios::binary};
		if (!stream)
		{
			return std::nullopt;
		}

		const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
		return ParseSubagentMeta(text, stem.substr(kAgentPrefix.size()));
	}

	std::filesystem::path SubagentTranscriptPath(const std::filesystem::path& sessionFile, std::string_view agentId)
	{
		return SubagentDirectory(sessionFile) / Core::FromUtf8(std::string{kAgentPrefix} + std::string{agentId} + ".jsonl");
	}
}
