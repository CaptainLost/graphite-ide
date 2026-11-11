#include <doctest/doctest.h>

#include "Wsl/WslServerInstalls.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/ChildProcess.h"
#include "Core/Remote/AgentDeployment.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <string>

TEST_CASE("Servers are installed into a distro under its user's home, named by the share")
{
	CHECK(Graphite::DistroServersDirectory("Ubuntu", "/home/me").native() ==
		  std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\.graphite\\servers"}.native());
	CHECK(Graphite::DistroServersDirectory("Debian", "/root").native() == std::filesystem::path{L"\\\\wsl.localhost\\Debian\\root\\.graphite\\servers"}.native());
}

TEST_CASE("A path that is not in the distro is not made executable there")
{
	const Graphite::ServerInstaller::MakeExecutableStep step = Graphite::MakeExecutableInDistro("Ubuntu", L"C:\\Windows\\System32\\wsl.exe");

	CHECK_FALSE(step(std::filesystem::path{L"C:\\servers\\clangd.exe"}));
	CHECK_FALSE(step(std::filesystem::path{L"\\\\wsl.localhost\\Debian\\home\\me\\clangd"}));
}

// The real distro: a script written through the share is not executable until the step runs.
TEST_CASE("A program written into the real distro through its share runs once made executable there")
{
	std::error_code error;

	if (!Core::IsWslAvailable() || !std::filesystem::exists(L"\\\\wsl.localhost\\Ubuntu\\tmp", error))
	{
		MESSAGE("no WSL Ubuntu here: skipped");
		return;
	}

	const std::string name = "graphite-wslinstall-" + std::to_string(_getpid());
	const std::filesystem::path folder = std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu\\tmp\\"} / Core::FromUtf8(name);
	std::filesystem::create_directories(folder, error);
	std::ofstream{folder / "tool", std::ios::binary} << "#!/bin/sh\nexit 7\n";

	const auto exitCodeOfRunning = [&]() -> std::optional<std::uint32_t> {
		Core::Result<Core::Scope<Core::ChildProcess>> run =
			Core::ChildProcess::Start(Core::WslExecutable(), {"-d", "Ubuntu", "--exec", "/tmp/" + name + "/tool"}, Core::WslExecutable().parent_path());
		REQUIRE(run.HasValue());
		REQUIRE(run.GetValue()->WaitForExit(std::chrono::seconds{20}));
		return run.GetValue()->GetExitCode();
	};

	CHECK(exitCodeOfRunning() != std::optional<std::uint32_t>{7});

	const Graphite::ServerInstaller::MakeExecutableStep step = Graphite::MakeExecutableInDistro("Ubuntu", Core::WslExecutable());
	const Core::Result<void> made = step(folder / "tool");
	REQUIRE_MESSAGE(made.HasValue(), (made ? "" : made.GetError().Message));

	CHECK(exitCodeOfRunning() == std::optional<std::uint32_t>{7});
	std::filesystem::remove_all(folder, error);
}
