#include <doctest/doctest.h>

#include "Wsl/WslAgents.h"

#include "FakeAgent.h"

#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/WslFileOperations.h"
#include "Core/Platform/Process.h"
#include "Core/Remote/AgentDeployment.h"

#include <filesystem>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using AgentFixtures::FakeAgent;

namespace {

	// Hands WslAgents a fresh fake agent per connection, so a test can count connections and hang
	// one up.
	struct FakeDistro
	{
		std::mutex Mutex;
		std::vector<std::unique_ptr<FakeAgent>> Agents;
		std::vector<std::string> Requests;
		std::atomic<int> Connections{0};
		bool Refuse = false;

		Graphite::WslAgents::Connector Connector()
		{
			return [this](const std::string&) -> Core::Result<std::shared_ptr<Core::AgentConnection>> {
				if (Refuse)
				{
					return Core::Error{Core::ErrorCode::NotFound, "no such distro"};
				}

				++Connections;
				auto agent = std::make_unique<FakeAgent>([this](const Core::AgentRequest& request, FakeAgent& self) {
					{
						std::scoped_lock lock{Mutex};
						Requests.push_back(request.Operation + " " + request.Arguments.value("path", std::string{}) + " " + request.Payload);
					}
					self.Reply(request.Id, {});
				});
				std::shared_ptr<Core::AgentConnection> connection{Core::AgentConnection::Open(agent->HostEnd(), "v1")};
				connection->WaitUntilSettled(2s);

				std::scoped_lock lock{Mutex};
				Agents.push_back(std::move(agent));
				return connection;
			};
		}
	};
}

TEST_CASE("A write under a WSL share reaches the distro's agent with the Linux path and the bytes")
{
	FakeDistro distro;
	Graphite::WslAgents agents{distro.Connector()};

	REQUIRE(agents.WriteFile(Core::WslSharePath{"Ubuntu", "/home/me/a.txt"}, "saved").HasValue());

	std::scoped_lock lock{distro.Mutex};
	REQUIRE(distro.Requests.size() == 1);
	CHECK(distro.Requests.front() == "writeFile /home/me/a.txt saved");
}

TEST_CASE("Two uses of one distro share one agent")
{
	FakeDistro distro;
	Graphite::WslAgents agents{distro.Connector()};

	REQUIRE(agents.Get("Ubuntu", 2s).HasValue());
	REQUIRE(agents.Get("Ubuntu", 2s).HasValue());

	CHECK(distro.Connections == 1);
}

TEST_CASE("A distro named in two cases is one distro with one agent")
{
	FakeDistro distro;
	Graphite::WslAgents agents{distro.Connector()};

	REQUIRE(agents.Get("Ubuntu", 2s).HasValue());
	REQUIRE(agents.Get("ubuntu", 2s).HasValue());

	CHECK(distro.Connections == 1);
}

TEST_CASE("After the agent stops, the next use starts another")
{
	FakeDistro distro;
	Graphite::WslAgents agents{distro.Connector()};

	REQUIRE(agents.Get("Ubuntu", 2s).HasValue());
	{
		std::scoped_lock lock{distro.Mutex};
		distro.Agents.back()->Hang();
	}
	std::this_thread::sleep_for(100ms);

	REQUIRE(agents.Get("Ubuntu", 2s).HasValue());
	CHECK(distro.Connections == 2);
}

TEST_CASE("Each new agent is handed to the connected hook once, ready")
{
	FakeDistro distro;
	std::atomic<int> handed{0};
	std::atomic<bool> wasReady{false};
	Graphite::WslAgents agents{distro.Connector(), [&](const std::string& name, Core::AgentConnection& connection) {
								   // The distro's name as WSL takes it, folded to lower case.
								   wasReady = name == "ubuntu" && connection.GetState() == Core::AgentConnection::State::Ready;
								   ++handed;
							   }};

	REQUIRE(agents.Get("Ubuntu", 2s).HasValue());
	REQUIRE(agents.Get("Ubuntu", 2s).HasValue());

	CHECK(handed == 1);
	CHECK(wasReady);
}

// A save into a WSL folder the way the editor makes it: Core's WriteFileAtomically on the share
// path, through the registered agents, into the real distro. Skipped, saying why, on a machine
// without WSL or without the Linux build of the agent.
TEST_CASE("A save under a WSL share lands in the distro through its agent, replacing the file there")
{
	if (!Core::IsWslAvailable())
	{
		MESSAGE("WSL is not available here: skipped");
		return;
	}

	const Core::Result<std::filesystem::path> here = Core::ExecutableDirectory();
	REQUIRE(here.HasValue());
#ifdef CORE_DEBUG
	const std::filesystem::path agentBinary = here.GetValue().parent_path().parent_path() / "Debug-linux-x86_64" / "GraphiteAgent" / "graphite-agent";
#else
	const std::filesystem::path agentBinary = here.GetValue().parent_path().parent_path() / "Release-linux-x86_64" / "GraphiteAgent" / "graphite-agent";
#endif

	if (!std::filesystem::exists(agentBinary))
	{
		MESSAGE("no Linux build of graphite-agent: build CoreLinux first; skipped");
		return;
	}

	std::error_code missing;

	if (!std::filesystem::exists(L"\\\\wsl.localhost\\Ubuntu\\tmp", missing))
	{
		MESSAGE("no WSL distro named Ubuntu here: skipped");
		return;
	}

	Graphite::WslAgents agents{Graphite::WslAgents::ConnectThroughWsl(agentBinary)};

	// Taken away again however the test ends, so no later case writes through a dead object.
	struct Registered
	{
		explicit Registered(Core::WslFileOperations* operations) { Core::SetWslFileOperations(operations); }
		~Registered() { Core::SetWslFileOperations(nullptr); }
	} registered{&agents};

	const Core::Result<std::shared_ptr<Core::AgentConnection>> ready = agents.Get("Ubuntu", 60s);
	REQUIRE_MESSAGE(ready.HasValue(), (ready ? "" : ready.GetError().Message));

	const std::filesystem::path folder{L"\\\\wsl.localhost\\Ubuntu\\tmp\\graphite-save-test"};
	std::error_code error;
	std::filesystem::create_directories(folder, error);
	const std::filesystem::path file = folder / "saved.txt";

	// Twice: the second replaces an existing file, which ReplaceFileW cannot do on the share.
	const Core::Result<void> first = Core::WriteFileAtomically(file, "first");
	const Core::Result<void> second = Core::WriteFileAtomically(file, "second");

	REQUIRE_MESSAGE(first.HasValue(), (first ? "" : first.GetError().Message));
	REQUIRE_MESSAGE(second.HasValue(), (second ? "" : second.GetError().Message));
	const Core::Result<std::string> contents = Core::ReadFileBytes(file);
	REQUIRE(contents.HasValue());
	CHECK(contents.GetValue() == "second");

	std::filesystem::remove_all(folder, error);
}

TEST_CASE("A distro that cannot be reached gives its reason")
{
	FakeDistro distro;
	distro.Refuse = true;
	Graphite::WslAgents agents{distro.Connector()};

	const auto connection = agents.Get("Nowhere", 2s);

	REQUIRE_FALSE(connection.HasValue());
	CHECK(connection.GetError().Message.find("no such distro") != std::string::npos);
}
