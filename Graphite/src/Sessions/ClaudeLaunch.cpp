#include "Sessions/ClaudeLaunch.h"

namespace Graphite {

	std::filesystem::path ClaudeProjectsDirectory(const std::optional<std::filesystem::path>& configDirectory,
												  const std::filesystem::path& home)
	{
		const std::filesystem::path root = configDirectory.has_value() ? *configDirectory : home / ".claude";
		return root / "projects";
	}

	std::filesystem::path ResolveClaudeProgram(
		const std::filesystem::path& configured,
		const std::function<std::filesystem::path(const std::string&)>& findOnPath)
	{
		std::error_code status;

		if (!configured.empty() && std::filesystem::is_regular_file(configured, status))
			return configured;

		return findOnPath("claude.exe");
	}

	ClaudeCommand NewSessionCommand(const std::filesystem::path& program, const std::string& sessionId)
	{
		return {program, {"--session-id", sessionId}};
	}

	ClaudeCommand ResumeCommand(const std::filesystem::path& program, const std::string& sessionId)
	{
		return {program, {"--resume", sessionId}};
	}
}
