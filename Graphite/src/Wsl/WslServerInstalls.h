#pragma once

#include "Language/Install/ServerInstaller.h"

#include <filesystem>
#include <string>

namespace Graphite {

	// Where language servers are installed into a distro: <home>/.graphite/servers, beside the
	// agent's own ~/.graphite/agent, named by its share path, which the installer writes through.
	std::filesystem::path DistroServersDirectory(const std::string& distro, const std::string& linuxHome);

	// chmod +x in the distro, through wsl.exe: a file written through the share is not executable
	// (measured on WSL 2). A path that is not in that distro fails.
	ServerInstaller::MakeExecutableStep MakeExecutableInDistro(std::string distro, std::filesystem::path wslExecutable);
}
