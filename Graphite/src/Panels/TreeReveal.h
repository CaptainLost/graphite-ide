#pragma once

#include "Solution/SolutionModel.h"

#include "Core/FileSystem/FileTree.h"

#include <filesystem>
#include <vector>

namespace Graphite {

	// Expands every folder between the tree's root and `file`, reading from disk any never
	// opened before, and returns the folders it went through, root first, so the view can
	// open the matching rows. The walk stops at the first folder that is not there; a file
	// outside the root expands nothing.
	std::vector<std::filesystem::path> ExpandToward(Core::FileTree& tree,
													const std::filesystem::path& file);

	// Whether `file` is listed anywhere under the given node of a solution, so the view knows
	// which rows to open on the way to it.
	bool HoldsFile(const ContentFolder& folder, const std::filesystem::path& file);
	bool HoldsFile(const Project& project, const std::filesystem::path& file);
	bool HoldsFile(const Folder& folder, const std::filesystem::path& file);
}
