#include <doctest/doctest.h>

#include "Core/Platform/CurrentPlatform.h"

TEST_CASE("This build reports the platform it was compiled for")
{
	const Core::PlatformTarget platform = Core::CurrentPlatform();

#ifdef CORE_PLATFORM_WINDOWS
	CHECK(platform.System == Core::OperatingSystem::Windows);
#endif

#if defined(_M_X64) || defined(__x86_64__)
	CHECK(platform.Machine == Core::Architecture::X64);
#endif

	CHECK(platform == Core::CurrentPlatform());
}
