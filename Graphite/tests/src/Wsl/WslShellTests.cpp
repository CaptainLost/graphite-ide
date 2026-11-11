#include <doctest/doctest.h>

#include "Wsl/WslShell.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Remote/AgentDeployment.h"
#include "Core/Terminal/TerminalScreen.h"
#include "Core/Terminal/TerminalSession.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <process.h>
#include <string>
#include <thread>
#include <vector>

namespace {

	const std::filesystem::path kWsl{L"C:\\Windows\\System32\\wsl.exe"};

	std::u32string RowText(const Core::TerminalScreen& screen, std::uint16_t row)
	{
		std::u32string text;

		for (std::uint16_t column = 0; column < screen.GetColumns(); ++column)
		{
			text.push_back(screen.At(row, column).Code);
		}

		return text;
	}

	const Core::ShellProfile kPowerShell{.Name = "PowerShell", .Path = L"C:\\Program Files\\PowerShell\\7\\pwsh.exe", .Arguments = {"-NoLogo"}};
}

TEST_CASE("A terminal in a folder inside WSL runs the distro's own shell, starting in that folder")
{
	const Graphite::TerminalLaunch launch = Graphite::ShellFor(kPowerShell, std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"}, kWsl);

	CHECK(launch.Profile.Name == "Ubuntu");
	CHECK(launch.Profile.Path == kWsl);
	CHECK(launch.Profile.Arguments == std::vector<std::string>{"-d", "Ubuntu", "--cd", "/home/me/p"});
	// The console's own folder is a local one: the share may not answer before the distro starts.
	CHECK(launch.WorkingDirectory == kWsl.parent_path());
}

TEST_CASE("A terminal in a Windows folder keeps the shell that was chosen")
{
	const Graphite::TerminalLaunch launch = Graphite::ShellFor(kPowerShell, std::filesystem::path{L"C:\\work\\p"}, kWsl);

	CHECK(launch.Profile.Name == kPowerShell.Name);
	CHECK(launch.Profile.Path == kPowerShell.Path);
	CHECK(launch.Profile.Arguments == kPowerShell.Arguments);
	CHECK(launch.WorkingDirectory == std::filesystem::path{L"C:\\work\\p"});
}

TEST_CASE("A WSL folder named with spaces or a character Windows forbids reaches the distro as its Linux path")
{
	const Graphite::TerminalLaunch launch =
		Graphite::ShellFor(kPowerShell, std::filesystem::path{L"\\\\wsl$\\Ubuntu-22.04\\home\\me\\my project\\a\uF03Ab"}, kWsl);

	CHECK(launch.Profile.Arguments == std::vector<std::string>{"-d", "Ubuntu-22.04", "--cd", "/home/me/my project/a:b"});
	CHECK(launch.Profile.Name == "Ubuntu-22.04");
}

// The real distro: the shell started for a folder inside WSL prints that folder for pwd.
TEST_CASE("A terminal opened in a folder inside WSL starts there in the real distro")
{
	std::error_code error;

	if (!Core::IsWslAvailable() || !std::filesystem::exists(L"\\\\wsl.localhost\\Ubuntu\\tmp", error))
	{
		MESSAGE("no WSL Ubuntu here: skipped");
		return;
	}

	// Per process, and with a space: another test run at the same time would share the folder.
	const std::string name = "graphite wslshell " + std::to_string(_getpid());
	const std::filesystem::path folder = std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu\\tmp\\"} / Core::FromUtf8(name);
	std::filesystem::create_directories(folder, error);

	const Graphite::TerminalLaunch launch = Graphite::ShellFor(kPowerShell, folder, Core::WslExecutable());
	Core::Result<Core::Scope<Core::TerminalSession>> started = Core::TerminalSession::Start(launch.Profile, launch.WorkingDirectory, 120, 30);
	REQUIRE_MESSAGE(started.HasValue(), (started ? "" : started.GetError().Message));
	Core::TerminalSession& session = *started.GetValue();

	session.Send("pwd\r");

	const std::string pid = std::to_string(_getpid());
	const std::u32string expected = U"/tmp/graphite wslshell " + std::u32string(pid.begin(), pid.end());
	bool shown = false;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};

	while (!shown && std::chrono::steady_clock::now() < deadline)
	{
		session.ApplyQueuedOutput();

		for (std::uint16_t row = 0; row < session.GetScreen().GetRows() && !shown; ++row)
		{
			shown = RowText(session.GetScreen(), row).starts_with(expected);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds{50});
	}

	CHECK(shown);
	session.Send("exit\r");
	started.GetValue().reset();
	std::filesystem::remove_all(folder, error);
}
