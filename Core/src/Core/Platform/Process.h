#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Core {

	Result<std::filesystem::path> ExecutableDirectory();

	bool IsProcessInForeground();

	// A variable's text; empty when it is unset or set to nothing.
	std::optional<std::wstring> EnvironmentValue(const wchar_t* name);

	std::optional<std::filesystem::path> EnvironmentPath(const wchar_t* name);

	Result<std::filesystem::path> HomeDirectory();

	Result<std::filesystem::path> RoamingDataDirectory();

	Result<std::filesystem::path> LocalDataDirectory();

	Result<std::filesystem::path> SystemFontsDirectory();
}
