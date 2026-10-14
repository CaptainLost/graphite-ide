#pragma once

#include "Solution/SolutionModel.h"

#include "Core/FileSystem/FileTree.h"

#include <filesystem>
#include <vector>

namespace Graphite {

	std::vector<std::filesystem::path> ExpandToward(Core::FileTree& tree,
													const std::filesystem::path& file);

	bool HoldsFile(const ContentFolder& folder, const std::filesystem::path& file);
	bool HoldsFile(const Project& project, const std::filesystem::path& file);
	bool HoldsFile(const Folder& folder, const std::filesystem::path& file);
}
