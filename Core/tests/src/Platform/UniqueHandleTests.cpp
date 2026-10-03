#include <doctest/doctest.h>

#include "Core/Platform/UniqueHandle.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <utility>

namespace {
	bool IsOpen(HANDLE handle)
	{
		DWORD flags = 0;
		return ::GetHandleInformation(handle, &flags) != FALSE;
	}
}

TEST_CASE("UniqueHandle closes what it owns when it goes")
{
	const HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
	REQUIRE(event != nullptr);

	{
		const Core::UniqueHandle owner{event};
		CHECK(IsOpen(event));
	}

	CHECK_FALSE(IsOpen(event));
}

TEST_CASE("UniqueHandle hands ownership on when moved, and closes it once")
{
	const HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
	REQUIRE(event != nullptr);

	Core::UniqueHandle first{event};
	Core::UniqueHandle second = std::move(first);

	CHECK_FALSE(first);
	CHECK(second.Get() == event);

	second.Reset();
	CHECK_FALSE(IsOpen(event));
}

// The Win32 calls disagree about which of the two they return on failure.
TEST_CASE("UniqueHandle owns nothing for null or INVALID_HANDLE_VALUE")
{
	CHECK_FALSE(Core::UniqueHandle{nullptr});
	CHECK_FALSE(Core::UniqueHandle{INVALID_HANDLE_VALUE});
}
#endif
