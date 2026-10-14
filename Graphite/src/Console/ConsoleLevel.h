#pragma once

#include "Core/Base/Log.h"

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
}
