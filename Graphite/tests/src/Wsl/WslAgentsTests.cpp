#include <doctest/doctest.h>

#include "Wsl/WslAgents.h"

#include "FakeAgent.h"

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
								   wasReady = name == "Ubuntu" && connection.GetState() == Core::AgentConnection::State::Ready;
								   ++handed;
							   }};

	REQUIRE(agents.Get("Ubuntu", 2s).HasValue());
	REQUIRE(agents.Get("Ubuntu", 2s).HasValue());

	CHECK(handed == 1);
	CHECK(wasReady);
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
