#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <optional>

namespace Core {

	Result<std::filesystem::path> ExecutableDirectory();

	bool IsForeground();

	// The value of an environment variable read as a path, through the wide API so a profile
	// directory with a character outside the ANSI codepage survives. Unset and empty are the
	// same answer: neither names a directory.
	std::optional<std::filesystem::path> EnvironmentPath(const wchar_t* name);

	// The signed-in user's profile directory. An error when USERPROFILE is unset or names
	// something that is not a directory, so a caller never builds paths under nothing.
	Result<std::filesystem::path> HomeDirectory();
}