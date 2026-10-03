#pragma once

#include "Core/Base/Result.h"
#include "Solution/SolutionModel.h"

#include <filesystem>
#include <string>

namespace Graphite {

	Core::Result<Solution> ParseSln(const std::filesystem::path& path,
									const std::string& text);

}
