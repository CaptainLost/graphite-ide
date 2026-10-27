#include "corepch.h"

#include "Core/Platform/ThreadWindows.h"

#ifdef CORE_PLATFORM_WINDOWS

namespace Core {

	namespace {

		constexpr BOOL kKeepLooking = TRUE;
		constexpr BOOL kStopLooking = FALSE;

		struct ShownWindowSearch
		{
			HWND Found = nullptr;
		};

		// WARNING: A thread id is reused once its thread ends, so a window is taken only from this process: a dialog
		// that closed a moment ago must not hand its id to another program's window.
		bool IsOfThisProcess(HWND window)
		{
			DWORD process = 0;
			::GetWindowThreadProcessId(window, &process);
			return process == ::GetCurrentProcessId();
		}

		BOOL CALLBACK TakeIfShown(HWND window, LPARAM searchAddress)
		{
			if (!::IsWindowVisible(window) || !IsOfThisProcess(window))
			{
				return kKeepLooking;
			}

			reinterpret_cast<ShownWindowSearch*>(searchAddress)->Found = window;
			return kStopLooking;
		}
	}

	HWND FindShownWindowOfThread(DWORD thread)
	{
		ShownWindowSearch search;
		static_cast<void>(::EnumThreadWindows(thread, TakeIfShown, reinterpret_cast<LPARAM>(&search)));
		return search.Found;
	}

	// WARNING: Restored by a posted request rather than a sent one, so the thread that draws never waits on the
	// window's own thread.
	void BringWindowToFront(HWND window)
	{
		if (::IsIconic(window))
		{
			static_cast<void>(::ShowWindowAsync(window, SW_RESTORE));
		}

		static_cast<void>(::SetForegroundWindow(window));
	}
}

#endif
