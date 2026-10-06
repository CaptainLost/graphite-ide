#include "corepch.h"

#include "Core/Base/Assert.h"

#include <cstdio>
#include <string>

#ifdef CORE_PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace Core {

	void ReportAssertionFailure(const char* condition, const char* message, const char* file,
								int line)
	{
		// Shaped like a compiler diagnostic, file(line):, because the debugger's output window
		// makes exactly that shape double-clickable.
		const std::string report = std::string{file} + "(" + std::to_string(line) +
								   "): assertion failed: " + condition + "\n  " + message + "\n";

		// Written twice: the test runners are console builds read through stderr, while
		// Graphite.exe is a windowed build whose stderr goes nowhere and is read in the debugger.
		std::fputs(report.c_str(), stderr);

#ifdef CORE_PLATFORM_WINDOWS
		::OutputDebugStringA(report.c_str());
#endif
	}
}
