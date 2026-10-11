#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <string_view>

namespace Graphite {

	Core::Result<std::filesystem::path> FindServerBinary(std::string_view command,
														 const std::filesystem::path& configured);
}
