#include <doctest/doctest.h>

#include "SettingsPageFrames.h"
#include "TempAppSettings.h"

#include "Search/SearchSettings.h"
#include "Settings/Panel/FindInFilesPage.h"

#include <optional>
#include <string>
#include <vector>

namespace {

	struct FindInFilesScreen
	{
		TempAppSettings Settings;
		Graphite::SearchSettings Search{Settings.Get()};
		bool SolutionLoaded = false;
		Graphite::FindInFilesPage Page{Search, SolutionLoaded};
		SettingsPageFrames Frames{Page};
	};
}

TEST_CASE("The find in files page lists respecting .gitignore and limiting to project files")
{
	FindInFilesScreen screen;

	CHECK(screen.Frames.RowTitles() == std::vector<std::string>{"Respect .gitignore", "Only files listed in projects"});
}

TEST_CASE("Limiting to project files says why it cannot be used without a solution")
{
	FindInFilesScreen screen;

	const std::optional<Graphite::SettingsRow> withoutSolution = screen.Frames.FindRow("Only files listed in projects");
	REQUIRE(withoutSolution.has_value());
	CHECK(withoutSolution->Description == "No solution is open, so there are no project files to limit to.");

	screen.SolutionLoaded = true;

	const std::optional<Graphite::SettingsRow> withSolution = screen.Frames.FindRow("Only files listed in projects");
	REQUIRE(withSolution.has_value());
	CHECK(withSolution->Description == "Searches only the files the open solution's projects list.");
}

TEST_CASE("Respecting .gitignore is switched by its toggle")
{
	FindInFilesScreen screen;
	const bool respectedBefore = screen.Search.Get().RespectGitIgnore;

	screen.Frames.ClickControl("Respect .gitignore");

	CHECK(screen.Search.Get().RespectGitIgnore != respectedBefore);
}

TEST_CASE("Limiting to project files cannot be switched without a solution")
{
	FindInFilesScreen screen;
	const bool limitedBefore = screen.Search.Get().ProjectsOnly;

	screen.Frames.ClickControl("Only files listed in projects");

	CHECK(screen.Search.Get().ProjectsOnly == limitedBefore);
}
