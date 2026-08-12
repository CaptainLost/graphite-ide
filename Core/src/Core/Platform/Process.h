#pragma once

#include "Core/Base/Result.h"

#include <filesystem>

namespace Core {

	Result<std::filesystem::path> ExecutableDirectory();

	bool IsForeground();
}