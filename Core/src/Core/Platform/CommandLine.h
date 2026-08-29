#pragma once

#include "Core/Base/PlatformDetection.h"

#ifdef CORE_PLATFORM_WINDOWS

#include <filesystem>
#include <string>
#include <vector>

namespace Core {

	// CreateProcessW takes one command line rather than an argv array, so the pieces have to
	// be joined by hand. Quoting only when a space is present keeps the common case readable
	// in a debugger without changing what the child receives. This is not full Win32 argv
	// escaping — nothing this library launches needs an embedded quote — just enough to keep
	// a single argument, or the program path itself, from being split in two by
	// CreateProcessW's own space-splitting.
	std::wstring BuildWindowsCommandLine(const std::filesystem::path& program,
										 const std::vector<std::string>& arguments);
}

#endif
