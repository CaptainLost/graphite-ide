#include <doctest/doctest.h>

#include "SettingsPageFrames.h"
#include "TempAppSettings.h"

#include "Editor/EditorSettings.h"
#include "Settings/Panel/GitPage.h"

#include <string>
#include <vector>

namespace {

	struct GitScreen
	{
		TempAppSettings Settings;
		Graphite::EditorSettings Editor{Settings.Get()};
		Graphite::GitPage Page{Editor};
		SettingsPageFrames Frames{Page};
	};
}

TEST_CASE("The git page lists wrapping long lines in diffs")
{
	GitScreen screen;

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Diffs"});
	CHECK(screen.Frames.RowTitles() == std::vector<std::string>{"Wrap long lines"});
}

TEST_CASE("Wrapping long lines in diffs is the switch the diff toolbar has")
{
	GitScreen screen;
	const bool wrappedBefore = screen.Editor.IsDiffWordWrap();

	screen.Frames.ClickControl("Wrap long lines");

	CHECK(screen.Editor.IsDiffWordWrap() != wrappedBefore);
}
