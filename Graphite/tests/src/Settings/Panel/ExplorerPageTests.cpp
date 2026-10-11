#include <doctest/doctest.h>

#include "SettingsPageFrames.h"

#include "Settings/Panel/CommandControls.h"
#include "Settings/Panel/ExplorerPage.h"

#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"

#include <string>
#include <utility>
#include <vector>

namespace {

	// The Explorer's own commands, with the state they would change in the panel.
	struct ExplorerScreen
	{
		bool Follows = false;
		std::string Display = "show";
		Core::CommandRegistry Commands;
		Graphite::ExplorerPage Page{Commands};
		SettingsPageFrames Frames{Page};

		ExplorerScreen()
		{
			Commands.Add(Core::Command{.Id = "explorer.followActiveFile",
									   .Label = "Follow Active File",
									   .Run = [this] { Follows = !Follows; },
									   .Checked = [this] { return Follows; }});
			AddDisplay("git.ignored.show", "show");
			AddDisplay("git.ignored.dim", "dim");
			AddDisplay("git.ignored.hide", "hide");
		}

		void AddDisplay(std::string id, std::string display)
		{
			Commands.Add(Core::Command{.Id = std::move(id),
									   .Label = display,
									   .Run = [this, display] { Display = display; },
									   .Checked = [this, display] { return Display == display; }});
		}
	};
}

TEST_CASE("The explorer page lists following the active file and the files Git ignores")
{
	ExplorerScreen screen;

	CHECK(screen.Frames.RowTitles() == std::vector<std::string>{"Open folders to the active file", "Files ignored by Git"});
}

TEST_CASE("Opening folders to the active file is the command the View menu runs")
{
	ExplorerScreen screen;

	screen.Frames.ClickControl("Open folders to the active file");

	CHECK(screen.Follows);
}

TEST_CASE("Files ignored by Git shows the display the Explorer is in and runs the command of the one picked")
{
	ExplorerScreen screen;

	CHECK(Graphite::CheckedChoice(screen.Commands, Graphite::kIgnoredFileChoices) == 0);

	Graphite::RunChoice(screen.Commands, Graphite::kIgnoredFileChoices[2]);

	CHECK(screen.Display == "hide");
	CHECK(Graphite::CheckedChoice(screen.Commands, Graphite::kIgnoredFileChoices) == 2);
}

TEST_CASE("The explorer page draws wide and narrow without an ImGui assertion")
{
	ExplorerScreen screen;

	screen.Frames.DrawPageTwice(900.0f);
	screen.Frames.DrawPageTwice(320.0f);

	CHECK(screen.Display == "show");
}
