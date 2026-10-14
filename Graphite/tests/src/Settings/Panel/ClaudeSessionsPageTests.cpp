#include <doctest/doctest.h>

#include "SettingsPageFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Sessions/ClaudeSettings.h"
#include "Settings/Panel/ClaudeSessionsPage.h"

#include "Core/FileSystem/FileSystem.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

	constexpr float kWideWindowWidth = 900.0f;
	constexpr float kNarrowWindowWidth = 320.0f;

	struct ClaudeScreen
	{
		TempDirectory Temp;
		TempAppSettings Settings;
		Graphite::ClaudeSettings Claude{Settings.Get()};
		Graphite::ClaudeSessionsPage Page{Claude};
		SettingsPageFrames Frames{Page};
	};
}

TEST_CASE("The Claude page holds the program and the prices")
{
	ClaudeScreen screen;

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Program", "Prices"});
	CHECK(screen.Frames.RowTitles() == std::vector<std::string>{"Claude program", "Prices"});
}

TEST_CASE("The program row names the file that runs")
{
	ClaudeScreen screen;
	const std::filesystem::path program = screen.Temp.GetPath() / "claude.exe";
	std::ofstream{program} << "stand-in";
	screen.Claude.SetBinary(program);

	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Claude program");

	REQUIRE(row.has_value());
	CHECK(row->Description == "Runs " + Core::ToUtf8(program) + ".");
	CHECK(row->Problem.empty());
}

TEST_CASE("The Claude page draws wide and narrow without an ImGui assertion")
{
	ClaudeScreen screen;

	screen.Frames.DrawPageTwice(kWideWindowWidth);
	screen.Frames.DrawPageTwice(kNarrowWindowWidth);

	CHECK(screen.Frames.CardHeadings().size() == 2);
}
