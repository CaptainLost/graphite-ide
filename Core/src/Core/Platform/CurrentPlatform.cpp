#include "corepch.h"

#include "Core/Platform/CurrentPlatform.h"

namespace Core {

	PlatformTarget CurrentPlatform()
	{
#if defined(CORE_PLATFORM_WINDOWS)
		constexpr OperatingSystem kSystem = OperatingSystem::Windows;
#elif defined(__APPLE__)
		constexpr OperatingSystem kSystem = OperatingSystem::MacOS;
#else
		constexpr OperatingSystem kSystem = OperatingSystem::Linux;
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
		constexpr Architecture kMachine = Architecture::Arm64;
#else
		constexpr Architecture kMachine = Architecture::X64;
#endif

		return PlatformTarget{.System = kSystem, .Machine = kMachine};
	}
}
