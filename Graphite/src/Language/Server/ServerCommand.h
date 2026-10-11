#pragma once

#include "Language/Api/LanguageService.h"
#include "Language/Server/DotnetRuntime.h"
#include "Language/Server/ServerCatalog.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Graphite {

	struct ServerCommandPlaces
	{
		std::filesystem::path ServersDirectory;
		std::vector<DotnetInstallation> Dotnet;
	};

	struct ServerCommand
	{
		// Starting when the command may run; any other state says why it may not, and Detail says it in words.
		ServerState State = ServerState::Starting;
		std::string Detail;
		std::filesystem::path Program;
		std::vector<std::string> LeadingArguments;
		std::filesystem::path InstalledFile;
	};

	// A configured path that does not exist is NotFound even when another copy could be found: running a server
	// other than the one configured would hide the mistake.
	ServerCommand FindServerCommand(const ServerDefinition& server, const std::filesystem::path& configured,
									const ServerCommandPlaces& places);

	std::string DescribeCommand(const ServerCommand& command);
}
