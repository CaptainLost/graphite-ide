#include <doctest/doctest.h>

#include "SettingsPageFrames.h"
#include "TempAppSettings.h"

#include "Settings/AnalysisSettings.h"
#include "Settings/LanguageServers/BackgroundAnalysisPage.h"

#include <format>
#include <optional>
#include <string>
#include <vector>

namespace {

	struct AnalysisScreen
	{
		TempAppSettings Settings;
		Graphite::AnalysisSettings Analysis{Settings.Get()};
		Graphite::BackgroundAnalysisPage Page{Analysis};
		SettingsPageFrames Frames{Page};
	};
}

TEST_CASE("The background analysis page lists the pass, its memory and how many files it sends at once")
{
	AnalysisScreen screen;

	CHECK(screen.Frames.RowTitles() == std::vector<std::string>{"Analyse in the background", "Analysis kept",
																"Set the number of files automatically", "Files analysed at once"});
}

TEST_CASE("The background analysis is switched by its toggle")
{
	AnalysisScreen screen;
	const bool enabledBefore = screen.Analysis.IsEnabled();

	screen.Frames.ClickControl("Analyse in the background");

	CHECK(screen.Analysis.IsEnabled() != enabledBefore);
}

TEST_CASE("The number of files says the automatic figure while it is automatic")
{
	AnalysisScreen screen;

	screen.Analysis.SetConcurrencyAutomatic(true);
	const std::optional<Graphite::SettingsRow> automatic = screen.Frames.FindRow("Files analysed at once");
	REQUIRE(automatic.has_value());
	CHECK(automatic->Description == std::format("Automatic: {}.", screen.Analysis.GetConcurrency()));

	screen.Analysis.SetConcurrencyAutomatic(false);
	const std::optional<Graphite::SettingsRow> chosen = screen.Frames.FindRow("Files analysed at once");
	REQUIRE(chosen.has_value());
	CHECK(chosen->Description == "How many files are sent to the server together.");
}

TEST_CASE("The background analysis page draws on and off, wide and narrow, without an ImGui assertion")
{
	AnalysisScreen screen;

	screen.Frames.DrawPageTwice(900.0f);
	screen.Analysis.SetEnabled(false);
	screen.Frames.DrawPageTwice(320.0f);

	CHECK_FALSE(screen.Analysis.IsEnabled());
}
