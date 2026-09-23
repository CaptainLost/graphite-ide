#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct ClaudeCommand
	{
		std::filesystem::path Program;
		std::vector<std::string> Arguments;
	};

	// CLAUDE_CONFIG_DIR moves everything Claude Code keeps; without it, ~/.claude.
	std::filesystem::path ClaudeProjectsDirectory(const std::optional<std::filesystem::path>& configDirectory,
												  const std::filesystem::path& home);

	// The configured program when it exists, else claude.exe from PATH, else nothing. The
	// lookup is passed in so the rule is tested without depending on this machine's PATH.
	std::filesystem::path ResolveClaudeProgram(
		const std::filesystem::path& configured,
		const std::function<std::filesystem::path(const std::string&)>& findOnPath);

	// Started under an id the IDE chose, so the session's tab knows which transcript is its
	// own before Claude has written a line of it.
	ClaudeCommand NewSessionCommand(const std::filesystem::path& program, const std::string& sessionId);

	ClaudeCommand ResumeCommand(const std::filesystem::path& program, const std::string& sessionId);
}
