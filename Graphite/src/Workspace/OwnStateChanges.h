#pragma once

#include <filesystem>
#include <vector>

namespace Graphite {

	bool OnlyOwnStateChanged(const std::vector<std::filesystem::path>& directories,
							 const std::filesystem::path& root);
}
