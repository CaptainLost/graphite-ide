#pragma once

#include "Core/Base/Log.h"

#include <array>
#include <string_view>

namespace Graphite {

	// The levels the console's minimum-level combo offers, quietest threshold first. Critical is
	// not among them: it is above every threshold, so it is shown whatever is chosen.
	inline constexpr std::array<Core::LogLevel, 4> kConsoleLevels{
		Core::LogLevel::Trace, Core::LogLevel::Info, Core::LogLevel::Warn, Core::LogLevel::Error};

	// What the console hides unless asked: the trace chatter the loggers record for the file.
	inline constexpr Core::LogLevel kDefaultConsoleLevel = Core::LogLevel::Info;

	// Whether a message of `level` is drawn when the reader wants `minimum` and above. Only the
	// shown list is cut by this; the loggers keep recording everything to the log file.
	bool IsShownAtLevel(Core::LogLevel level, Core::LogLevel minimum);

	// The word the combo shows for a level, in the case a menu uses rather than the log's capitals.
	std::string_view ConsoleLevelName(Core::LogLevel level);
}
