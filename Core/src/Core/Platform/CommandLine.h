#pragma once

#include "Core/Base/PlatformDetection.h"

#include <string>
#include <string_view>

namespace Core {

	// Quotes one argument, when it needs quoting, so the CommandLineToArgvW rules split it back out unchanged. It works on
	// UTF-8 as it stands: every character those rules look at is ASCII, and no byte of a longer character is one of them.
	std::string QuoteWindowsArgument(std::string_view argument);
}

#ifdef CORE_PLATFORM_WINDOWS

#include <filesystem>
#include <vector>

namespace Core {

	// cmd.exe does not split its line by the CommandLineToArgvW rules this escapes for: it treats
	// quotes and characters such as & and | its own way. An argument that reaches cmd.exe must be
	// one this library chose, never text taken from the user or a workspace.
	std::wstring BuildWindowsCommandLine(const std::filesystem::path& program,
										 const std::vector<std::string>& arguments);
}

#endif