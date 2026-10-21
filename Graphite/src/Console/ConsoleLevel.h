#pragma once

#include "Core/Base/Log.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace Graphite {

	inline constexpr std::array<Core::LogLevel, 4> kConsoleLevels{
		Core::LogLevel::Trace, Core::LogLevel::Info, Core::LogLevel::Warn, Core::LogLevel::Error};

	inline constexpr Core::LogLevel kDefaultConsoleLevel = Core::LogLevel::Info;

	bool IsShownAtLevel(Core::LogLevel level, Core::LogLevel minimum);

	std::string_view ConsoleLevelName(Core::LogLevel level);

	std::string ConsoleEntryCount(std::size_t shownCount, std::size_t totalCount);

	// Read from the scheme in use each frame, so an entry stays legible on a light scheme as on a dark one.
	struct ConsoleLevelColours
	{
		ImU32 Text = 0;
		ImU32 Quiet = 0;
		ImU32 Warning = 0;
		ImU32 Error = 0;
	};

	ImU32 ColourOfConsoleLevel(Core::LogLevel level, const ConsoleLevelColours& colours);
}
