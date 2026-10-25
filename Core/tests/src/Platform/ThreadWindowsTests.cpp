#include <doctest/doctest.h>

#ifdef CORE_PLATFORM_WINDOWS

#include "Core/Platform/ThreadWindows.h"

#include <future>
#include <thread>

namespace {

	// WARNING: Far off every screen, never activated and kept off the taskbar, so a test run takes nobody's focus.
	constexpr int kFarOffScreen = -32000;
	constexpr int kOnePixel = 1;
	constexpr DWORD kQuietWindowStyles = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

	struct ThreadWithWindows
	{
		DWORD Thread = 0;
		HWND Hidden = nullptr;
		HWND Shown = nullptr;
	};

	HWND MakeQuietWindow()
	{
		return ::CreateWindowExW(kQuietWindowStyles, L"STATIC", L"", WS_POPUP, kFarOffScreen, kFarOffScreen, kOnePixel,
								 kOnePixel, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
	}

	// WARNING: A window belongs to the thread that made it and must be destroyed there, so the windows live on a thread
	// of their own until the test lets it go.
	class WindowThread
	{
	public:
		explicit WindowThread(bool showsOne)
		{
			std::promise<ThreadWithWindows> made;
			std::future<ThreadWithWindows> madeWindows = made.get_future();
			std::shared_future<void> release = m_Release.get_future().share();

			m_Thread = std::thread([showsOne, made = std::move(made), release]() mutable {
				ThreadWithWindows windows{.Thread = ::GetCurrentThreadId(), .Hidden = MakeQuietWindow()};

				if (showsOne)
				{
					windows.Shown = MakeQuietWindow();
					::ShowWindow(windows.Shown, SW_SHOWNOACTIVATE);
				}

				made.set_value(windows);
				release.wait();

				if (windows.Shown != nullptr)
				{
					::DestroyWindow(windows.Shown);
				}

				::DestroyWindow(windows.Hidden);
			});

			m_Windows = madeWindows.get();
		}

		~WindowThread()
		{
			m_Release.set_value();
			m_Thread.join();
		}

		WindowThread(const WindowThread&) = delete;
		WindowThread& operator=(const WindowThread&) = delete;

		const ThreadWithWindows& GetWindows() const { return m_Windows; }

	private:
		std::promise<void> m_Release;
		std::thread m_Thread;
		ThreadWithWindows m_Windows;
	};
}

TEST_CASE("The window a thread shows is found among the ones it keeps hidden")
{
	constexpr bool kShowsOne = true;
	const WindowThread owner{kShowsOne};
	const ThreadWithWindows& windows = owner.GetWindows();
	REQUIRE(windows.Hidden != nullptr);
	REQUIRE(windows.Shown != nullptr);

	CHECK(Core::FindShownWindowOfThread(windows.Thread) == windows.Shown);
}

TEST_CASE("A thread that keeps every window hidden has none found")
{
	constexpr bool kShowsNone = false;
	const WindowThread owner{kShowsNone};
	const ThreadWithWindows& windows = owner.GetWindows();
	REQUIRE(windows.Hidden != nullptr);

	CHECK(Core::FindShownWindowOfThread(windows.Thread) == nullptr);
}

#endif
