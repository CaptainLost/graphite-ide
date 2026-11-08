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

	// The install roots Git for Windows' installer recorded in the registry, machine-wide first, then per user, then a
	// 32-bit install; a portable copy records none.
	std::vector<std::filesystem::path> GitForWindowsInstallRoots();

	// %ProgramFiles%\Git, where Git for Windows installs unless told otherwise, whether or not anything is there; empty
	// when ProgramFiles is unset.
	std::filesystem::path DefaultGitForWindowsRoot();
}
