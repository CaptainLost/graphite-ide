#include <doctest/doctest.h>

#include "Core/Platform/UniqueHandle.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <utility>

namespace {
	constexpr BOOL kManualReset = TRUE;
	constexpr BOOL kInitialStateUnsignalled = FALSE;

	HANDLE CreateUnnamedEvent()
	{
		return ::CreateEventW(nullptr, kManualReset, kInitialStateUnsignalled, nullptr);
	}

	bool IsOpen(HANDLE handle)
	{
		DWORD handleFlags = 0;
		return ::GetHandleInformation(handle, &handleFlags) != FALSE;
	}
}

TEST_CASE("UniqueHandle closes what it owns when it goes")
{
	const HANDLE event = CreateUnnamedEvent();
	REQUIRE(event != nullptr);

	{
		const Core::UniqueHandle owner{event};
		CHECK(IsOpen(event));
	}

	CHECK_FALSE(IsOpen(event));
}

TEST_CASE("UniqueHandle hands ownership on when moved, and closes it once")
{
	const HANDLE event = CreateUnnamedEvent();
	REQUIRE(event != nullptr);

	Core::UniqueHandle movedFrom{event};
	Core::UniqueHandle movedTo = std::move(movedFrom);

	CHECK_FALSE(movedFrom);
	CHECK(movedTo.Get() == event);

	movedTo.Reset();
	CHECK_FALSE(IsOpen(event));
}

TEST_CASE("UniqueHandle owns nothing for null or INVALID_HANDLE_VALUE since the Win32 calls disagree about which of the two reports a failure")
{
	CHECK_FALSE(Core::UniqueHandle{nullptr});
	CHECK_FALSE(Core::UniqueHandle{INVALID_HANDLE_VALUE});
}
#endif
