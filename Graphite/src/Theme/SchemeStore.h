#pragma once

#include "Theme/ColorScheme.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// The names of the .json files in the directory, without their extension, minus any that
	// collide with a built-in — a file named dark.json is ignored so the built-in keeps its
	// name.
	std::vector<std::string> ListSchemes(const std::filesystem::path& directory);

	// Never fails. A file that is missing, unreadable or malformed yields a scheme with the dark
	// base and no overrides, and warns — because a scheme on a drive that is not plugged in
	// should come back when the drive does, not take the editor down while it is out.
	ColorScheme LoadScheme(const std::filesystem::path& file);

	// False when the name is a built-in or the write fails. Writes only the colours the scheme
	// actually overrides, so it stays sparse and keeps surviving a new token kind.
	bool SaveScheme(const std::filesystem::path& directory, std::string_view name,
					const ColorScheme& scheme);

	bool DeleteScheme(const std::filesystem::path& directory, std::string_view name);
}
