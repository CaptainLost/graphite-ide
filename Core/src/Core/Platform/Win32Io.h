#pragma once

// Private to Core's .cpp files: it pulls in <windows.h>, so a public header must never include it.

#ifdef CORE_PLATFORM_WINDOWS

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace Core {

	// A synchronous write may take less than it was given, and is asked again for the rest. One that takes nothing at all
	// would take nothing the next time either, so it counts as failed rather than spinning.
	inline bool WriteAll(HANDLE handle, std::string_view bytes)
	{
		constexpr std::size_t kLargestWrite = MAXDWORD;

		std::size_t bytesWritten = 0;

		while (bytesWritten < bytes.size())
		{
			const DWORD bytesThisCall = static_cast<DWORD>((std::min)(bytes.size() - bytesWritten, kLargestWrite));

			DWORD bytesWrittenThisCall = 0;
			const BOOL wrote = ::WriteFile(handle, bytes.data() + bytesWritten, bytesThisCall, &bytesWrittenThisCall, nullptr);
			const bool writeStalled = !wrote || bytesWrittenThisCall == 0;

			if (writeStalled)
			{
				return false;
			}

			bytesWritten += bytesWrittenThisCall;
		}

		return true;
	}

	// The OVERLAPPED and the buffer behind it are the caller's, and the kernel writes into both until the cancelled
	// operation is collected, so this waits for the cancellation before the caller may let either go.
	inline void CancelAndCollect(HANDLE handle, OVERLAPPED& overlapped)
	{
		DWORD discardedByteCount = 0;
		const BOOL waitForCancellation = TRUE;

		::CancelIoEx(handle, &overlapped);
		::GetOverlappedResult(handle, &overlapped, &discardedByteCount, waitForCancellation);
	}
}

#endif
