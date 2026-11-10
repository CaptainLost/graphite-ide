#include <doctest/doctest.h>

#include "Wsl/WslAgents.h"
#include "Wsl/WslFolderWatch.h"

#include "FakeAgent.h"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
using AgentFixtures::FakeAgent;

namespace {

	// One fake agent that answers watch with id 7 and then reports a change below the root.
	struct WatchingDistro
	{
		std::mutex Mutex;
		std::unique_ptr<FakeAgent> Agent;

		Graphite::WslAgents::Connector Connector()
		{
			return [this](const std::string&) -> Core::Result<std::shared_ptr<Core::AgentConnection>> {
				auto agent = std::make_unique<FakeAgent>([](const Core::AgentRequest& request, FakeAgent& self) {
					if (request.Operation == "watch")
					{
						self.Reply(request.Id, {{"watch", 7}});
						self.Send(Core::EncodeEvent(Core::AgentEvent{
							.Name = "changes",
							.Body = {{"watch", 7},
									 {"directories", nlohmann::json::array({"/home/me/p/src"})},
									 {"overflowed", false},
									 {"ignoreRules", true},
									 {"repositoryState", false}}}));
						return;
					}

					self.Reply(request.Id, {});
				});
				std::shared_ptr<Core::AgentConnection> connection{Core::AgentConnection::Open(agent->HostEnd(), "v1")};
				connection->WaitUntilSettled(2s);

				std::scoped_lock lock{Mutex};
				Agent = std::move(agent);
				return connection;
			};
		}
	};

	bool Eventually(const std::function<bool()>& check)
	{
		const auto deadline = std::chrono::steady_clock::now() + 3s;

		while (std::chrono::steady_clock::now() < deadline)
		{
			if (check())
			{
				return true;
			}

			std::this_thread::sleep_for(20ms);
		}

		return false;
	}
}

TEST_CASE("A change the agent reports below the root comes back as the folder's share path")
{
	WatchingDistro distro;
	Graphite::WslAgents agents{distro.Connector()};
	const Core::Scope<Graphite::WslFolderWatch> watch = Graphite::WslFolderWatch::Start(agents, Core::WslSharePath{"Ubuntu", "/home/me/p"});

	Core::FolderChanges changes;
	REQUIRE(Eventually([&] {
		changes = watch->TakeSettled();
		return !changes.Directories.empty();
	}));

	REQUIRE(changes.Directories.size() == 1);
	CHECK(changes.Directories.front().native() == L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p\\src");
	CHECK(changes.IgnoreRulesChanged);
	CHECK(watch->IsRunning());
}

TEST_CASE("A watch stops running when its agent stops")
{
	WatchingDistro distro;
	Graphite::WslAgents agents{distro.Connector()};
	const Core::Scope<Graphite::WslFolderWatch> watch = Graphite::WslFolderWatch::Start(agents, Core::WslSharePath{"Ubuntu", "/home/me/p"});
	REQUIRE(Eventually([&] { return !watch->TakeSettled().Directories.empty(); }));

	{
		std::scoped_lock lock{distro.Mutex};
		distro.Agent->Hang();
	}

	CHECK(Eventually([&] { return !watch->IsRunning(); }));
}
