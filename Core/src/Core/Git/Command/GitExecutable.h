#pragma once

#include <filesystem>
#include <optional>

namespace Core {

	// The git.exe on PATH, else the one under the install root Git for Windows' installer recorded, else the one under
	// %ProgramFiles%\Git; empty when none of them exists.
	std::optional<std::filesystem::path> FindGit();
}
