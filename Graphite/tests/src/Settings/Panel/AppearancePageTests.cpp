#include <doctest/doctest.h>

#include "SettingsPageFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Settings/AppSettings.h"
#include "Settings/Panel/AppearancePage.h"
#include "Theme/ActiveScheme.h"
#include "Theme/ColorScheme.h"
#include "UI/ModalGate.h"

#include "Core/Events/EventBus.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace {

	struct AppearanceScreen
	{
		TempDirectory Themes;
		TempAppSettings Settings;
		Graphite::ActiveScheme Scheme;
		Graphite::ModalGate Modals;
		Core::EventBus Events;
		Graphite::AppearancePage Page{Scheme, Settings.Get(), Modals, Events, Themes.GetPath()};
		SettingsPageFrames Frames{Page};
	};

	constexpr std::size_t kThemeAndInterfaceRows = 3;
}

TEST_CASE("The appearance page holds the theme, the interface and a card for each group of colours")
{
	AppearanceScreen screen;

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Theme", "Interface", "Syntax colours", "Editor colours",
																   "Application colours", "Git colours", "Claude sessions colours"});
}

TEST_CASE("Every colour of every group has a row of its own")
{
	AppearanceScreen screen;

	const std::size_t colours = static_cast<std::size_t>(Graphite::TokenKind::Count) + static_cast<std::size_t>(Graphite::EditorColor::Count) +
								static_cast<std::size_t>(Graphite::ChromeColor::Count) + static_cast<std::size_t>(Graphite::GitColor::Count) +
								static_cast<std::size_t>(Graphite::SessionColor::Count);

	CHECK(screen.Frames.RowTitles().size() == kThemeAndInterfaceRows + colours);
}

TEST_CASE("A built-in scheme says it cannot be edited")
{
	AppearanceScreen screen;

	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("This scheme");

	REQUIRE(row.has_value());
	CHECK(row->Description == "Built-in schemes cannot be edited. Duplicate one to change it.");
}

TEST_CASE("Duplicating a built-in scheme chooses an editable copy")
{
	AppearanceScreen screen;
	const std::string before = screen.Settings.Get().GetColorScheme();

	screen.Frames.ClickControl("This scheme");

	CHECK(screen.Settings.Get().GetColorScheme() != before);
	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("This scheme");
	REQUIRE(row.has_value());
	CHECK(row->Description == "A copy of your own: rename it, delete it, or change its colours below.");
}

TEST_CASE("The appearance page draws wide and narrow, built-in and own, without an ImGui assertion")
{
	AppearanceScreen screen;

	screen.Frames.DrawPageTwice(900.0f);
	screen.Frames.DrawPageTwice(320.0f);
	screen.Frames.ClickControl("This scheme");
	screen.Frames.DrawPageTwice(900.0f);

	CHECK_FALSE(screen.Modals.IsBusy());
}
