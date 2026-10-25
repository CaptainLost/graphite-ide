#include <doctest/doctest.h>

#include "Terminal/TerminalWindows.h"
#include "UI/ToastEvent.h"
#include "Workspace/Workspace.h"

#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Events/EventBus.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
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

	CHECK(fixture.Terminals.OpenCommand("Build", fixture.Temp.GetPath() / "missing.exe", noArguments,
										fixture.Temp.GetPath()) == nullptr);

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

	CHECK(fixture.Terminals.OpenCommand("Build", fixture.Temp.GetPath() / "missing.exe", noArguments,
										fixture.Temp.GetPath()) == nullptr);

	CHECK_FALSE(fixture.Terminals.IsHidden());
}

namespace {
	constexpr std::chrono::seconds kCommandExitLimit{10};
	constexpr std::chrono::milliseconds kFrameInterval{20};

	void UpdateUntilExited(Graphite::TerminalWindows& terminals, const Graphite::CommandTerminal& command)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kCommandExitLimit;

		while (!command.Exited && std::chrono::steady_clock::now() < deadline)
		{
			terminals.Update();
			std::this_thread::sleep_for(kFrameInterval);
		}
	}
}

TEST_CASE("A command's terminal stays open after its program exits and tells whoever started it how it ended")
{
	TerminalFixture fixture;
	const std::vector<std::string> exitWithThree{"/c", "exit 3"};
	constexpr std::uint32_t kExpectedExitCode = 3;

	const Core::Ref<Graphite::CommandTerminal> command =
		fixture.Terminals.OpenCommand("Build", "cmd.exe", exitWithThree, fixture.Temp.GetPath());

	REQUIRE(command != nullptr);
	CHECK_FALSE(command->Exited);

	UpdateUntilExited(fixture.Terminals, *command);

	CHECK(command->Exited);
	CHECK(command->ExitCode == std::optional<std::uint32_t>{kExpectedExitCode});
	CHECK_FALSE(command->Closed);
	CHECK(fixture.Terminals.GetOpenCount() == 1);
}

TEST_CASE("Asking to see a command's terminal shows hidden terminals")
{
	TerminalFixture fixture;
	const std::vector<std::string> waitForInput{"/k"};

	const Core::Ref<Graphite::CommandTerminal> command =
		fixture.Terminals.OpenCommand("Build", "cmd.exe", waitForInput, fixture.Temp.GetPath());

	REQUIRE(command != nullptr);
	fixture.Terminals.SetHidden(true);
	command->ShowRequested = true;

	fixture.Terminals.Update();

	CHECK_FALSE(fixture.Terminals.IsHidden());
	CHECK_FALSE(command->ShowRequested);
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
