#pragma once

#include "Language/Install/ServerInstaller.h"
#include "Language/Server/ServerCatalog.h"

#include <string_view>

namespace Graphite {

	class LanguageServices;
	class ServerInstalls;

	bool UninstallStoppingServers(ServerInstalls& installs, LanguageServices& services, std::string_view serverId);

	void RestartServersAfter(const InstallProgress& outcome, LanguageServices& services);

	// WARNING: Decided from the install record, never from the server's state, which an untrusted folder holds at Restricted.
	bool MayInstall(const ServerInstalls& installs, const ServerDefinition& server);

	// WARNING: Only the languages whose chosen server this is: restarting Roslyn because csharp-ls changed would reload a
	// solution for nothing.
	void ReconfigureLanguagesRunning(LanguageServices& services, std::string_view serverId);
}
