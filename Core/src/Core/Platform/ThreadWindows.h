#pragma once

// WARNING: Private to Core's .cpp files and their tests: it pulls in <windows.h>, so a public header must never include it.

#ifdef CORE_PLATFORM_WINDOWS

#include <windows.h>

namespace Core {

	HWND FindShownWindowOfThread(DWORD thread);

	void BringWindowToFront(HWND window);
}

#endif
