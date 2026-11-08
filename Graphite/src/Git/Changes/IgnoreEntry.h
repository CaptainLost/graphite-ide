#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Graphite {

	// The .gitignore line that ignores exactly this path of the working tree: anchored at the root with a leading slash,
	// separated by '/', and ending in '/' for a directory, so a file of the same name elsewhere stays tracked.
	std::string IgnoreLineFor(const std::filesystem::path& relative, bool isDirectory);

	// The existing text of a .gitignore with the line added on a line of its own, unless it is already there.
	std::string WithIgnoreLine(std::string_view existing, std::string_view line);

	// Adds the path to the .gitignore at the working tree's root, creating the file when there is none.
	Core::Result<void> AddToGitIgnore(const std::filesystem::path& workingDirectory, const std::filesystem::path& relative,
									  bool isDirectory);
}
