#include "Sessions/Transcript/SubagentLinks.h"

#include "Core/FileSystem/FileSystem.h"

#include "Sessions/Transcript/JsonAccess.h"
#include "Sessions/SessionSummary.h"

#include <nlohmann/json.hpp>

namespace Graphite {

	namespace {
		constexpr std::string_view kAgentPrefix = "agent-";
	}

	std::optional<std::pair<std::string, SubagentInfo>> ParseSubagentMeta(std::string_view json, std::string agentId)
	{
		constexpr bool kThrowOnMalformedJson = false;
		const nlohmann::json parsed = nlohmann::json::parse(json.begin(), json.end(), nullptr, kThrowOnMalformedJson);
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
		std::optional<std::string> agentId = SubagentIdOf(transcript);
		if (!agentId)
		{
			return std::nullopt;
		}

		std::filesystem::path metaFile = transcript;
		metaFile.replace_extension(".meta.json");

		const Core::Result<std::string> text = Core::ReadFileBytes(metaFile);
		if (!text)
		{
			return std::nullopt;
		}

		return ParseSubagentMeta(text.GetValue(), std::move(*agentId));
	}

	std::optional<std::string> SubagentIdOf(const std::filesystem::path& transcript)
	{
		const std::string stem = Core::ToUtf8(transcript.stem());

		if (transcript.extension() != ".jsonl" || !stem.starts_with(kAgentPrefix))
		{
			return std::nullopt;
		}

		return stem.substr(kAgentPrefix.size());
	}

	bool ForEachSubagentTranscript(
		const std::filesystem::path& sessionFile,
		const std::function<void(const std::filesystem::path& transcript, const std::string& agentId)>& visit)
	{
		std::error_code directoryStatus;

		for (std::filesystem::directory_iterator entryIterator{SubagentDirectory(sessionFile), directoryStatus}, endOfDirectory;
			 !directoryStatus && entryIterator != endOfDirectory; entryIterator.increment(directoryStatus))
		{
			if (const std::optional<std::string> agentId = SubagentIdOf(entryIterator->path()))
			{
				visit(entryIterator->path(), *agentId);
			}
		}

		return !directoryStatus || directoryStatus == std::errc::no_such_file_or_directory;
	}

	std::filesystem::path SubagentTranscriptPath(const std::filesystem::path& sessionFile, std::string_view agentId)
	{
		return SubagentDirectory(sessionFile) / Core::FromUtf8(std::string{kAgentPrefix} + std::string{agentId} + ".jsonl");
	}
}
