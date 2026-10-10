#pragma once

#include <filesystem>
#include <optional>

namespace Graphite {

	// Reads the root's directory listing, so it belongs where a server is about to start, never in a frame.
	std::optional<std::filesystem::path> SolutionForRoot(const std::filesystem::path& root, const std::filesystem::path& selected);
}
