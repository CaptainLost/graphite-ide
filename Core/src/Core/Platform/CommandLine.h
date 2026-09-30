#pragma once

#include "Core/Base/PlatformDetection.h"

#ifdef CORE_PLATFORM_WINDOWS

#include <filesystem>
#include <string>
#include <vector>

namespace Core {

	// CreateProcessW takes one command line rather than an argv array, so the pieces have to
	// be joined by hand. Arguments are UTF-8 and escaped by the rules CommandLineToArgvW and
	// the C runtime use to split the line again, so the child receives exactly the strings
	// passed here: empty ones, embedded quotes and trailing backslashes included. Quoting
	// only when an argument needs it keeps the common case readable in a debugger.
	//
	// cmd.exe does not split its line by these rules - it has its own treatment of quotes
	// and of characters such as & and |. An argument that reaches cmd.exe must be one this
	// library chose, never text taken from the user or a workspace.
	std::wstring BuildWindowsCommandLine(const std::filesystem::path& program,
										 const std::vector<std::string>& arguments);
}

#endif
