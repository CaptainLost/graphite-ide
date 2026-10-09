#pragma once

#include "Language/Server/ServerCatalog.h"

#include <nlohmann/json.hpp>

#include <filesystem>
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
		nlohmann::json InitializationOptions;
		nlohmann::json Settings;
	};

	ServerLaunch LaunchFor(const ServerDefinition& server, std::filesystem::path binary, const ServerOverrides& overrides);
}
