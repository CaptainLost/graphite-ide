#pragma once

#include "Language/Server/ServerCatalog.h"

#include "Core/Platform/ChildProcess.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

	// For a server that names files its own way (one in a WSL distro): each JSON-RPC body rewritten
	// on its way to the server and on its way back. Empty for a server that shares Graphite's paths.
	struct BodyTranslation
	{
		std::function<std::string(std::string_view body)> ToServer;
		std::function<std::string(std::string_view body)> FromServer;
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
		BodyTranslation Translation;

		// What the status says when the program exits with 127, a POSIX shell's "command not found",
		// for a server started through one; empty for any other.
		std::string WhenProgramMissing;
	};

	struct LaunchPlaces
	{
		std::vector<std::string> LeadingArguments;
		std::filesystem::path LogDirectory;
		std::filesystem::path DotnetRoot;
		std::filesystem::path TemporaryDirectory;
	};

	ServerLaunch LaunchFor(const ServerDefinition& server, std::filesystem::path program, const ServerOverrides& overrides,
						   const LaunchPlaces& places = {});

	std::string DescribeProgram(const std::filesystem::path& program, std::span<const std::string> leadingArguments);
}
