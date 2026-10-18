#include <doctest/doctest.h>

#include "Terminal/TerminalWindows.h"
#include "UI/ToastEvent.h"
#include "Workspace/Workspace.h"

#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Events/EventBus.h"

#include <filesystem>
#include <string>
#include <vector>

namespace {
	struct TerminalFixture
	{
		TerminalFixture()
		{
			Events.Subscribe<Graphite::ToastEvent>([this](const Graphite::ToastEvent& event) { Toasts.push_back(event); });
		}

		TempDirectory Temp;
		TempAppSettings Settings;
		Core::EventBus Events;
		Graphite::Workspace Workspace{Events, Settings.Get()};
		Core::Keymap Keys;
		Core::CommandRegistry Commands;
		Graphite::TerminalWindows Terminals{Workspace, Keys, Commands, Settings.Get(), Events};
		std::vector<Graphite::ToastEvent> Toasts;
	};
}

TEST_CASE("A program that cannot be started is reported in an application toast")
{
	TerminalFixture fixture;
	const std::vector<std::string> noArguments;

	CHECK_FALSE(fixture.Terminals.OpenCommand("Build", fixture.Temp.GetPath() / "missing.exe", noArguments,
											  fixture.Temp.GetPath()));

	REQUIRE(fixture.Toasts.size() == 1);
	CHECK(fixture.Toasts.front().Kind == Core::ToastKind::Error);
	CHECK(fixture.Toasts.front().Title == "Terminal failed to start");
	CHECK(fixture.Toasts.front().Message.find("Build") != std::string::npos);
	CHECK(fixture.Toasts.front().Scope == Graphite::ToastScope::Application);
}

TEST_CASE("Asking for a shell that is not there is reported in a warning toast")
{
	TerminalFixture fixture;
	const std::filesystem::path noDirectory;

	CHECK_FALSE(fixture.Terminals.OpenShellIn(fixture.Terminals.GetShells().size(), noDirectory));

	REQUIRE(fixture.Toasts.size() == 1);
	CHECK(fixture.Toasts.front().Kind == Core::ToastKind::Warning);
	CHECK(fixture.Toasts.front().Title == "No shell found");
	CHECK(fixture.Toasts.front().Scope == Graphite::ToastScope::Application);
}

TEST_CASE("Terminals start shown")
{
	TerminalFixture fixture;

	CHECK_FALSE(fixture.Terminals.IsHidden());
}

TEST_CASE("A program asked to run in a terminal shows hidden terminals even when it fails to start")
{
	TerminalFixture fixture;
	const std::vector<std::string> noArguments;
	fixture.Terminals.SetHidden(true);

	CHECK_FALSE(fixture.Terminals.OpenCommand("Build", fixture.Temp.GetPath() / "missing.exe", noArguments,
											  fixture.Temp.GetPath()));

	CHECK_FALSE(fixture.Terminals.IsHidden());
}

TEST_CASE("Reset Layout shows hidden terminals")
{
	TerminalFixture fixture;
	fixture.Terminals.SetHidden(true);

	fixture.Terminals.RequestRedock();

	CHECK_FALSE(fixture.Terminals.IsHidden());
}

TEST_CASE("Restoring a folder whose saved list is empty leaves hidden terminals hidden")
{
	TerminalFixture fixture;
	fixture.Terminals.SetHidden(true);

	fixture.Terminals.RestoreState(std::vector<Graphite::TerminalTabState>{}, true);

	CHECK(fixture.Terminals.IsHidden());
	CHECK(fixture.Terminals.GetOpenCount() == 0);
}

TEST_CASE("With no terminal the strip reads closed even while the flag is set")
{
	TerminalFixture fixture;
	fixture.Terminals.SetHidden(true);

	const Graphite::TerminalStripLook look = fixture.Terminals.GetStripLook();

	CHECK_FALSE(look.Open);
	CHECK_FALSE(look.RunningHidden);
}
