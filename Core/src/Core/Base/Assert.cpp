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
		// The file and line are formatted the way a compiler emits a diagnostic, so the
		// debugger's output window turns the first line into something double-clickable.
		const std::string report = std::string{file} + "(" + std::to_string(line) +
								   "): assertion failed: " + condition + "\n  " + message + "\n";

		// stderr first, because a console build - the test runners - is where this is read
		// without a debugger attached.
		std::fputs(report.c_str(), stderr);

#ifdef CORE_PLATFORM_WINDOWS
		// And the debugger, because Graphite.exe is a windowed application with no console:
		// its stderr goes nowhere, so before this the only sign of a failed assertion was the
		// break itself, with the message that explains it discarded.
		::OutputDebugStringA(report.c_str());
#endif
	}
}
