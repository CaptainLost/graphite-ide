#pragma once

#include <cstdint>

namespace Core {

	enum class OperatingSystem : std::uint8_t
	{
		Windows,
		Linux,
		MacOS
	};

	enum class Architecture : std::uint8_t
	{
		X64,
		Arm64
	};

	struct PlatformTarget
	{
		OperatingSystem System = OperatingSystem::Windows;
		Architecture Machine = Architecture::X64;

		bool operator==(const PlatformTarget&) const = default;
	};

	// What this build was compiled for, which is what the programs it starts must run on.
	PlatformTarget CurrentPlatform();
}
