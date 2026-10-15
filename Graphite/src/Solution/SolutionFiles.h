#pragma once

#include <filesystem>
#include <vector>

namespace Graphite {

	// WARNING: An .slnx hides the .sln of the same name, which is the older format of the same
	// solution, so a folder holding both offers the solution once.
	std::vector<std::filesystem::path> FindSolutions(const std::filesystem::path& folder);

	bool ListsSolutionBefore(const std::filesystem::path& left, const std::filesystem::path& right);
}
