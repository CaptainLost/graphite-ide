#include <doctest/doctest.h>

#include "Core/Remote/AgentConnection.h"
#include "Core/Remote/AgentDeployment.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Process.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

using namespace std::chrono_literals;

namespace {

	// The Linux build this test binary sits beside: bin/<Configuration>-windows-x86_64/CoreTests/
	// next to bin/<Configuration>-linux-x86_64/GraphiteAgent/.
	std::optional<std::filesystem::path> BuiltAgent()
	{
		const Core::Result<std::filesystem::path> here = Core::ExecutableDirectory();

		if (!here)
		{
			return std::nullopt;
		}

#ifdef CORE_DEBUG
		constexpr const char* kLinuxFolder = "Debug-linux-x86_64";
#else
		constexpr const char* kLinuxFolder = "Release-linux-x86_64";
#endif

		const std::filesystem::path agent = here.GetValue().parent_path().parent_path() / kLinuxFolder / "GraphiteAgent" / "graphite-agent";
		std::error_code error;
		return std::filesystem::exists(agent, error) ? std::optional{agent} : std::nullopt;
	}

	std::string Read(const std::filesystem::path& file)
	{
		std::ifstream stream{file, std::ios::binary};
		std::ostringstream contents;
		contents << stream.rdbuf();
		return contents.str();
	}
}

// Installs the real agent into the default distro, talks to it through wsl.exe, and checks what
// it writes and what it watches through the distro's share. Skipped, saying why, on a machine
// without WSL or without the Linux build.
TEST_CASE("The agent installs into WSL, writes a file and reports a change, end to end")
{
	if (!Core::IsWslAvailable())
	{
		MESSAGE("WSL is not available here: the end-to-end agent test is skipped");
		return;
	}

	const std::optional<std::filesystem::path> agentBinary = BuiltAgent();

	if (!agentBinary)
	{
		MESSAGE("no Linux build of graphite-agent beside this test binary: build CoreLinux first; skipped");
		return;
	}

	const Core::Result<Core::AgentInstall> installed = Core::InstallAgent("", *agentBinary);
	REQUIRE_MESSAGE(installed.HasValue(), (installed ? "" : installed.GetError().Message));
	const Core::AgentInstall& install = installed.GetValue();

	Core::Result<Core::Scope<Core::AgentPipe>> pipe = Core::StartAgent(install);
	REQUIRE(pipe.HasValue());

	const Core::Scope<Core::AgentConnection> connection = Core::AgentConnection::Open(std::move(pipe.GetValue()), install.Version);
	REQUIRE(connection->WaitUntilSettled(30s));
	REQUIRE_MESSAGE(connection->GetState() == Core::AgentConnection::State::Ready,
					(connection->GetFailure() ? connection->GetFailure()->Message : std::string{}));
	CHECK(connection->GetHome().starts_with("/"));

	const std::string folder = "/tmp/graphite-agent-e2e";
	const std::filesystem::path folderOnShare = Core::SharePathOf(install.Distro, folder);
	std::error_code ignored;
	std::filesystem::remove_all(folderOnShare, ignored);

	REQUIRE(connection->SendAndWait("makeDirectory", {{"path", folder}}, {}, 10s).HasValue());
	REQUIRE(connection->SendAndWait("writeFile", {{"path", folder + "/a b.txt"}}, "written by the agent", 10s).HasValue());
	CHECK(Read(folderOnShare / "a b.txt") == "written by the agent");

	auto changed = std::make_shared<std::promise<nlohmann::json>>();
	auto once = std::make_shared<std::once_flag>();
	connection->SetEventHandler([changed, once](const Core::AgentEvent& event) {
		if (event.Name == "changes")
		{
			std::call_once(*once, [&] { changed->set_value(event.Body); });
		}
	});

	const Core::Result<Core::AgentReply> watched = connection->SendAndWait("watch", {{"root", folder}}, {}, 10s);
	REQUIRE(watched.HasValue());

	std::ofstream{folderOnShare / "from windows.txt"} << "written through the share";

	auto future = changed->get_future();
	REQUIRE(future.wait_for(5s) == std::future_status::ready);
	CHECK(future.get().at("directories") == nlohmann::json::array({folder}));

	std::filesystem::remove_all(folderOnShare, ignored);
}
