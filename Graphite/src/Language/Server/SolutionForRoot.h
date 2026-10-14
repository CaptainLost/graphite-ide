#pragma once

#include <filesystem>
#include <optional>

namespace Graphite {

	std::optional<std::filesystem::path> SolutionForRoot(const std::filesystem::path& root, const std::filesystem::path& selected);
}
