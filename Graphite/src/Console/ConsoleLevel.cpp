#include "Console/ConsoleLevel.h"

#include "Core/Base/Strings.h"

namespace Graphite {

	bool IsShownAtLevel(Core::LogLevel level, Core::LogLevel minimum)
	{
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

	std::string ConsoleEntryCount(std::size_t shownCount, std::size_t totalCount)
	{
		if (shownCount == totalCount)
		{
			return Core::Counted(totalCount, "entry", "entries");
		}

		return std::to_string(shownCount) + " / " + Core::Counted(totalCount, "entry", "entries");
	}

	ImU32 ColourOfConsoleLevel(Core::LogLevel level, const ConsoleLevelColours& colours)
	{
		switch (level)
		{
		case Core::LogLevel::Trace:
			return colours.Quiet;
		case Core::LogLevel::Info:
			return colours.Text;
		case Core::LogLevel::Warn:
			return colours.Warning;
		case Core::LogLevel::Error:
		case Core::LogLevel::Critical:
			return colours.Error;
		}

		return colours.Text;
	}
}
