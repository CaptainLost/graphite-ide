#include <doctest/doctest.h>

#include "SettingsPageFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Settings/Panel/TerminalPage.h"
#include "Terminal/TerminalWindows.h"
#include "Workspace/Workspace.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Events/EventBus.h"

#include <string>
#include <vector>

namespace {

	struct TerminalScreen
	{
		TempDirectory Temp;
		TempAppSettings Settings;
		Core::EventBus Events;
		Graphite::Workspace Workspace{Events, Settings.Get()};
		Core::Keymap Keys;
		Core::CommandRegistry Commands;
		Graphite::TerminalWindows Terminals{Workspace, Keys, Commands, Settings.Get(), Events};
		Graphite::TerminalPage Page{Settings.Get(), Terminals};
		SettingsPageFrames Frames{Page};
	};
}

TEST_CASE("The terminal page offers the default shell")
{
	TerminalScreen screen;

	CHECK(screen.Frames.RowTitles() == std::vector<std::string>{"Default shell"});
}

TEST_CASE("The terminal page draws whatever shells this machine has without an ImGui assertion")
{
	TerminalScreen screen;

	screen.Frames.DrawPageTwice(900.0f);
	screen.Frames.DrawPageTwice(320.0f);

	CHECK(screen.Frames.RowTitles().size() == 1);
}
