#pragma once

#include "Language/Api/LanguageService.h"
#include "Language/Server/DotnetRuntime.h"
#include "Language/Server/ServerCatalog.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Graphite {

	struct ServerCommandPlaces
	{
		std::filesystem::path ServersDirectory;
		std::function<std::vector<DotnetInstallation>()> ListDotnet;
	};

	struct ServerCommand
	{
		ServerState State = ServerState::Starting;
		std::string Detail;
		std::filesystem::path Program;
		std::vector<std::string> LeadingArguments;
		std::filesystem::path InstalledFile;
		std::filesystem::path DotnetRoot;
	};

	ServerCommand FindServerCommand(const ServerDefinition& server, const std::filesystem::path& configured,
									const ServerCommandPlaces& places);

	std::string DescribeCommand(const ServerCommand& command);
}
