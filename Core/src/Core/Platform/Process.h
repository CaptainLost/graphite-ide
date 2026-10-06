#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <optional>

namespace Core {

	Result<std::filesystem::path> ExecutableDirectory();

	bool IsProcessInForeground();

	std::optional<std::filesystem::path> EnvironmentPath(const wchar_t* name);

	Result<std::filesystem::path> HomeDirectory();

	Result<std::filesystem::path> RoamingDataDirectory();

	Result<std::filesystem::path> LocalDataDirectory();

	Result<std::filesystem::path> SystemFontsDirectory();
}
