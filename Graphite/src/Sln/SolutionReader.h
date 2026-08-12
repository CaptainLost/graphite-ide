#pragma once

#include "Core/Base/Result.h"
#include "Sln/SolutionModel.h"

#include <filesystem>

namespace Graphite {

	bool IsSolutionFile(const std::filesystem::path& path);

	Core::Result<Solution> ReadSolution(const std::filesystem::path& path);
}