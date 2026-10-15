#pragma once

#include <string_view>

namespace Core {

	// Windows' rules, on every platform: a name a project carries from one machine to another must
	// open on each of them.
	bool IsValidFileName(std::string_view name);

	bool IsForbiddenInFileName(char character);

	// WARNING: Windows opens the device whatever the extension, and ignores spaces before it, so
	// "nul.tar.gz" and "nul .txt" both name the device.
	bool NamesWindowsDevice(std::string_view name);
}
