#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Core {

	struct ShellProfile
	{
		std::string Name;
		std::filesystem::path Path;
		std::vector<std::string> Arguments;
	};

	// What is installed on this machine, in the order a terminal should offer it: the most
	// capable shell first, so opening a terminal without choosing gives the best default.
	// A shell that is not installed is simply absent — this reports, it does not fail.
	std::vector<ShellProfile> DiscoverShells();

	// The first directory on PATH holding `executableName`, joined to it; empty when none
	// does. The name must carry its extension: this does not try PATHEXT.
	std::filesystem::path FindOnPath(const std::string& executableName);
}
