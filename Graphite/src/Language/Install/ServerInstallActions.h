#pragma once

#include "Language/Install/ServerInstaller.h"
#include "Language/Server/ServerCatalog.h"

#include <string_view>

namespace Graphite {

	class LanguageServices;
	class ServerInstalls;

	// A running server holds its files, so the servers running it are stopped first and held stopped until the job ends.
	bool UninstallStoppingServers(ServerInstalls& installs, LanguageServices& services, std::string_view serverId);

	// Restarts what a finished job changed: a version placed by an install, and the servers any uninstall stopped.
	void RestartServersAfter(const InstallProgress& outcome, LanguageServices& services);

	// Decided from the install record, never from the server's state, which an untrusted folder holds at Restricted.
	bool MayInstall(const ServerInstalls& installs, const ServerDefinition& server);

	// Only the languages whose chosen server this is: restarting Roslyn because csharp-ls changed would reload a solution for nothing.
	void ReconfigureLanguagesRunning(LanguageServices& services, std::string_view serverId);
}
