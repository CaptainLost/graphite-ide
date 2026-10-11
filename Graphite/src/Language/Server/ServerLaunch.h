#pragma once

#include "Language/Server/ServerCatalog.h"

#include "Core/Platform/ChildProcess.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Graphite {

	class ServerAdapter;

	struct ServerOverrides
	{
		std::filesystem::path Binary;
		std::vector<std::string> ExtraArguments;
		nlohmann::json InitializationOptions;
		nlohmann::json Settings;
	};

	struct ServerLaunch
	{
		const ServerDefinition* Server = nullptr;
		const ServerAdapter* Adapter = nullptr;
		std::filesystem::path Binary;
		std::vector<std::string> Arguments;
		std::string Description;
		std::vector<Core::EnvironmentVariable> Environment;
		nlohmann::json InitializationOptions;
		nlohmann::json Settings;
		std::optional<std::filesystem::path> Solution;
		std::vector<std::filesystem::path> Projects;
	};

	struct LaunchPlaces
	{
		std::vector<std::string> LeadingArguments;
		std::filesystem::path LogDirectory;
		std::filesystem::path DotnetRoot;
	};

	ServerLaunch LaunchFor(const ServerDefinition& server, std::filesystem::path program, const ServerOverrides& overrides,
						   const LaunchPlaces& places = {});

	std::string DescribeProgram(const std::filesystem::path& program, std::span<const std::string> leadingArguments);
}
