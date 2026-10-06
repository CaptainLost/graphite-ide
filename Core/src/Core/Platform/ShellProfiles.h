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

	// The order is a preference, most capable first: a terminal opened without a choice takes the
	// first shell.
	std::vector<ShellProfile> DiscoverShells();

	// Empty when no PATH entry holds the file. PATHEXT is not tried, so the name must carry its
	// extension.
	std::filesystem::path FindOnPath(const std::string& executableFileName);
}
