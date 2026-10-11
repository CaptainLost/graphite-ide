#include <doctest/doctest.h>

#include "SettingsPageFrames.h"
#include "TempAppSettings.h"

#include "Editor/EditorSettings.h"
#include "Settings/Panel/EditorPage.h"

#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <string>
#include <vector>

namespace {

	struct EditorScreen
	{
		TempAppSettings Settings;
		Graphite::EditorSettings Editor{Settings.Get()};
		Core::CommandRegistry Commands;
		Core::Keymap Keys;
		Graphite::EditorPage Page{Editor, Commands};
		SettingsPageFrames Frames{Page};

		EditorScreen() { Editor.RegisterCommands(Commands, Keys); }
	};
}

TEST_CASE("The editor page lists its settings under Text, Indentation, Hover and Git changes")
{
	EditorScreen screen;

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Text", "Indentation", "Hover", "Git changes"});
	CHECK(screen.Frames.RowTitles() == std::vector<std::string>{"Font size", "Word wrap", "Indent with", "Tab width", "Symbol information",
																"Definition preview", "Hover delay", "Markers in the gutter",
																"Markers on the scrollbar"});
}

TEST_CASE("The word wrap toggle runs the command the View menu runs")
{
	EditorScreen screen;
	const bool wrappedBefore = screen.Editor.IsWordWrap();

	screen.Frames.ClickControl("Word wrap");

	CHECK(screen.Editor.IsWordWrap() != wrappedBefore);
	const Core::Command* command = screen.Commands.Find("editor.toggleWordWrap");
	REQUIRE(command != nullptr);
	CHECK(command->Checked() == screen.Editor.IsWordWrap());
}

TEST_CASE("The gutter markers toggle flips the markers")
{
	EditorScreen screen;
	const bool shownBefore = screen.Editor.HasGutterChangeMarkers();

	screen.Frames.ClickControl("Markers in the gutter");

	CHECK(screen.Editor.HasGutterChangeMarkers() != shownBefore);
}

TEST_CASE("The editor page draws wide and narrow without an ImGui assertion")
{
	EditorScreen screen;

	screen.Frames.DrawPageTwice(900.0f);
	screen.Frames.DrawPageTwice(320.0f);

	CHECK(screen.Frames.RowTitles().size() == 9);
}
