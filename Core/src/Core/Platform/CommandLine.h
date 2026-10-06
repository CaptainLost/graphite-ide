#pragma once

#include "Core/Base/PlatformDetection.h"

#ifdef CORE_PLATFORM_WINDOWS

#include <filesystem>
#include <string>
#include <vector>

namespace Core {

	// cmd.exe does not split its line by the CommandLineToArgvW rules this escapes for: it treats
	// quotes and characters such as & and | its own way. An argument that reaches cmd.exe must be
	// one this library chose, never text taken from the user or a workspace.
	std::wstring BuildWindowsCommandLine(const std::filesystem::path& program,
										 const std::vector<std::string>& arguments);
}

#endif
