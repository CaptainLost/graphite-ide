#include <doctest/doctest.h>

#include "SettingsPageFrames.h"
#include "TempAppSettings.h"

#include "Editor/EditorSettings.h"
#include "Settings/Panel/EditorPage.h"

#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace {

	constexpr float kWideWindowWidth = 900.0f;
	constexpr float kNarrowWindowWidth = 320.0f;
	constexpr std::size_t kRowCount = 14;

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
	CHECK(screen.Frames.RowTitles() == std::vector<std::string>{"Font size", "Word wrap", "Save when Graphite loses focus", "Highlight occurrences", "Indent with", "Tab width", "Line ending of new files", "Symbol information",
																"Definition preview", "Hover delay", "Markers in the gutter",
																"Markers on the scrollbar", "Line author", "Line author delay"});
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

TEST_CASE("The occurrence highlighting toggle flips the highlighting")
{
	EditorScreen screen;
	const bool highlightedBefore = screen.Editor.IsHighlightingOccurrences();

	screen.Frames.ClickControl("Highlight occurrences");

	CHECK(screen.Editor.IsHighlightingOccurrences() != highlightedBefore);
}

TEST_CASE("Line author setting row flips the setting")
{
	EditorScreen screen;
	const bool shownBefore = screen.Editor.HasLineAuthor();

	screen.Frames.ClickControl("Line author");

	CHECK(screen.Editor.HasLineAuthor() != shownBefore);
}

TEST_CASE("Line author setting is found by searching blame")
{
	EditorScreen screen;
	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Line author");

	REQUIRE(row.has_value());
	CHECK(std::find(row->Keywords.begin(), row->Keywords.end(), "blame") != row->Keywords.end());
}

TEST_CASE("Line author delay setting is found by searching blame or delay")
{
	EditorScreen screen;
	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Line author delay");

	REQUIRE(row.has_value());
	CHECK(row->Description == "How long the caret rests on a line before its author shows. 0 shows it at once.");
	CHECK(std::find(row->Keywords.begin(), row->Keywords.end(), "blame") != row->Keywords.end());
	CHECK(std::find(row->Keywords.begin(), row->Keywords.end(), "delay") != row->Keywords.end());
}

TEST_CASE("The editor page draws wide and narrow without an ImGui assertion")
{
	EditorScreen screen;

	screen.Frames.DrawPageTwice(kWideWindowWidth);
	screen.Frames.DrawPageTwice(kNarrowWindowWidth);

	CHECK(screen.Frames.RowTitles().size() == kRowCount);
}
