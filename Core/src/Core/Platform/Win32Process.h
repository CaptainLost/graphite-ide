#pragma once

// Private to Core's .cpp files: it pulls in <windows.h>, so a public header must never include it.

#ifdef CORE_PLATFORM_WINDOWS

#include <windows.h>

#include <cstdint>
#include <optional>

namespace Core {

	// WARNING: Asked only once the process handle has signalled: a program can exit with STILL_ACTIVE's own value, so
	// the code alone cannot tell a running program from one that ended.
	inline std::optional<std::uint32_t> ExitCodeOnceEnded(HANDLE process)
	{
		constexpr DWORD kPollWithoutWaiting = 0;

		const bool ended = process != nullptr && ::WaitForSingleObject(process, kPollWithoutWaiting) == WAIT_OBJECT_0;

		if (!ended)
		{
			return std::nullopt;
		}

		DWORD exitCode = 0;

		if (!::GetExitCodeProcess(process, &exitCode))
		{
			return std::nullopt;
		}

		return static_cast<std::uint32_t>(exitCode);
	}
}

#endif
