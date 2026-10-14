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

	std::filesystem::path ClaudeProjectsDirectory(const std::optional<std::filesystem::path>& configDirectory,
												  const std::filesystem::path& home);

	std::filesystem::path ResolveClaudeProgram(
		const std::filesystem::path& configured,
		const std::function<std::filesystem::path(const std::string&)>& findOnPath);

	ClaudeCommand NewSessionCommand(const std::filesystem::path& program, const std::string& sessionId);

	ClaudeCommand ResumeCommand(const std::filesystem::path& program, const std::string& sessionId);
}
