#include <doctest/doctest.h>

#include "Terminal/TerminalWindows.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/Palettes.h"
#include "UI/ToastEvent.h"
#include "Workspace/Workspace.h"

#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Events/EventBus.h"
#include "Core/ImGui/TabCloseRequests.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
		Core::TypingClaim Typing;
		Graphite::TerminalWindows Terminals{Workspace, Keys, Commands, Settings.Get(), Events, Typing};
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

namespace {
	constexpr std::string_view kTerminalIdentityMarker = "###terminal-";

	struct DrawnTerminals
	{
		ImGuiFrames Frames;
		const Graphite::EditorPalette Palette = Graphite::ReadEditorPalette(Graphite::DarkScheme());
		const Graphite::TerminalDockTargets NoDockTargets;
		Core::TabCloseRequests CloseRequests;

		// As the application runs a frame: output is applied, the windows drawn, and closed tabs removed after them.
		void Frame(Graphite::TerminalWindows& terminals)
		{
			Frames.Next([this, &terminals] {
				terminals.Update();
				terminals.Render(NoDockTargets, Palette, CloseRequests);
			});
			CloseRequests.EndFrame();
		}
	};

	std::optional<std::string> DrawnTerminalWindowName()
	{
		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			const std::string_view name = window->Name;

			if (window->Active && name.find(kTerminalIdentityMarker) != std::string_view::npos)
			{
				return std::string{name};
			}
		}

		return std::nullopt;
	}
}

TEST_CASE("A command's terminal stays open after its program exits and tells whoever started it how it ended")
{
	TerminalFixture fixture;
	DrawnTerminals drawn;
	const std::vector<std::string> exitWithThree{"/c", "exit 3"};
	constexpr std::uint32_t kExpectedExitCode = 3;

	const Core::Ref<Graphite::CommandTerminal> command =
		fixture.Terminals.OpenCommand("Build", "cmd.exe", exitWithThree, fixture.Temp.GetPath());

	REQUIRE(command != nullptr);
	CHECK_FALSE(command->Exited);

	UpdateUntilExited(fixture.Terminals, *command);
	drawn.Frame(fixture.Terminals);
	drawn.Frame(fixture.Terminals);

	CHECK(command->Exited);
	CHECK(command->ExitCode == std::optional<std::uint32_t>{kExpectedExitCode});
	CHECK_FALSE(command->Closed);
	CHECK(fixture.Terminals.GetOpenCount() == 1);
	CHECK(DrawnTerminalWindowName().has_value());
}

TEST_CASE("Closing an exited command's terminal removes its tab and tells whoever started it")
{
	TerminalFixture fixture;
	DrawnTerminals drawn;
	const std::vector<std::string> exitAtOnce{"/c", "exit 0"};

	const Core::Ref<Graphite::CommandTerminal> command =
		fixture.Terminals.OpenCommand("Build", "cmd.exe", exitAtOnce, fixture.Temp.GetPath());

	REQUIRE(command != nullptr);
	UpdateUntilExited(fixture.Terminals, *command);
	drawn.Frame(fixture.Terminals);

	const std::optional<std::string> windowName = DrawnTerminalWindowName();
	REQUIRE(windowName.has_value());

	drawn.CloseRequests.Request(*windowName);
	drawn.CloseRequests.EndFrame();
	drawn.Frame(fixture.Terminals);

	CHECK(fixture.Terminals.GetOpenCount() == 0);
	CHECK(command->Closed);
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
