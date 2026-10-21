#include <doctest/doctest.h>

#include "Console/ConsoleLevel.h"

using Core::LogLevel;

TEST_CASE("The default console level hides trace and shows everything from info up")
{
	CHECK(Graphite::kDefaultConsoleLevel == LogLevel::Info);

	CHECK_FALSE(Graphite::IsShownAtLevel(LogLevel::Trace, Graphite::kDefaultConsoleLevel));
	CHECK(Graphite::IsShownAtLevel(LogLevel::Info, Graphite::kDefaultConsoleLevel));
	CHECK(Graphite::IsShownAtLevel(LogLevel::Warn, Graphite::kDefaultConsoleLevel));
	CHECK(Graphite::IsShownAtLevel(LogLevel::Error, Graphite::kDefaultConsoleLevel));
	CHECK(Graphite::IsShownAtLevel(LogLevel::Critical, Graphite::kDefaultConsoleLevel));
}

TEST_CASE("The console count says how many entries are shown out of how many when some are hidden")
{
	CHECK(Graphite::ConsoleEntryCount(12, 12) == "12 entries");
	CHECK(Graphite::ConsoleEntryCount(0, 0) == "0 entries");
	CHECK(Graphite::ConsoleEntryCount(8, 12) == "8 / 12 entries");
	CHECK(Graphite::ConsoleEntryCount(0, 3) == "0 / 3 entries");
}

TEST_CASE("The console count says entry for a single one")
{
	CHECK(Graphite::ConsoleEntryCount(1, 1) == "1 entry");
	CHECK(Graphite::ConsoleEntryCount(0, 1) == "0 / 1 entry");
}

TEST_CASE("Trace as the minimum shows every message")
{
	for (const LogLevel level : {LogLevel::Trace, LogLevel::Info, LogLevel::Warn, LogLevel::Error, LogLevel::Critical})
	{
		CHECK(Graphite::IsShownAtLevel(level, LogLevel::Trace));
	}
}

TEST_CASE("Error as the minimum hides warnings but keeps errors and critical")
{
	CHECK_FALSE(Graphite::IsShownAtLevel(LogLevel::Warn, LogLevel::Error));
	CHECK(Graphite::IsShownAtLevel(LogLevel::Error, LogLevel::Error));
	CHECK(Graphite::IsShownAtLevel(LogLevel::Critical, LogLevel::Error));
}

TEST_CASE("Every level the combo offers has a name and critical is never hidden by one")
{
	for (const LogLevel minimum : Graphite::kConsoleLevels)
	{
		CHECK(Graphite::ConsoleLevelName(minimum) != "?");
		CHECK(Graphite::IsShownAtLevel(LogLevel::Critical, minimum));
	}
}

namespace {

	const Graphite::ConsoleLevelColours kColours{.Text = IM_COL32(1, 1, 1, 255),
												 .Quiet = IM_COL32(2, 2, 2, 255),
												 .Warning = IM_COL32(3, 3, 3, 255),
												 .Error = IM_COL32(4, 4, 4, 255)};
}

TEST_CASE("An info entry is drawn in the text colour and a trace entry in the quiet one")
{
	CHECK(Graphite::ColourOfConsoleLevel(LogLevel::Info, kColours) == kColours.Text);
	CHECK(Graphite::ColourOfConsoleLevel(LogLevel::Trace, kColours) == kColours.Quiet);
}

TEST_CASE("A warning is drawn in the warning colour and an error or critical entry in the error colour")
{
	CHECK(Graphite::ColourOfConsoleLevel(LogLevel::Warn, kColours) == kColours.Warning);
	CHECK(Graphite::ColourOfConsoleLevel(LogLevel::Error, kColours) == kColours.Error);
	CHECK(Graphite::ColourOfConsoleLevel(LogLevel::Critical, kColours) == kColours.Error);
}
