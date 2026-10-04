#include "Console/ConsoleLevel.h"

namespace Graphite {

	bool IsShownAtLevel(Core::LogLevel level, Core::LogLevel minimum)
	{
		// The enumerators are declared quietest first, so the order is the severity.
		return static_cast<int>(level) >= static_cast<int>(minimum);
	}

	std::string_view ConsoleLevelName(Core::LogLevel level)
	{
		switch (level)
		{
		case Core::LogLevel::Trace:
			return "Trace";
		case Core::LogLevel::Info:
			return "Info";
		case Core::LogLevel::Warn:
			return "Warn";
		case Core::LogLevel::Error:
			return "Error";
		case Core::LogLevel::Critical:
			return "Critical";
		}

		return "?";
	}

	std::string ConsoleEntryCount(std::size_t shown, std::size_t total)
	{
		if (shown == total)
		{
			return std::to_string(total) + " entries";
		}

		return std::to_string(shown) + " / " + std::to_string(total) + " entries";
	}
}
