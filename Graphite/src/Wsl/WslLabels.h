#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Graphite {

	// How a folder reads in lists such as Open Recent: "<distro>: <linux path>" for a folder
	// inside WSL, its path otherwise.
	std::string FolderDisplayName(const std::filesystem::path& folder);

	// The window title of a folder inside WSL, "<distro>: <name>"; empty for any other folder,
	// whose title is its name alone.
	std::optional<std::string> WslFolderTitle(const std::filesystem::path& folder);
}
