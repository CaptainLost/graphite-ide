#include <doctest/doctest.h>

#include "Git/RepositoryGitPort.h"
#include "Wsl/WslAgents.h"
#include "Wsl/WslGit.h"

#include "FakeAgent.h"
#include "GitFixture.h"

#include "Core/Remote/GitCodec.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using AgentFixtures::FakeAgent;

namespace {

	// A fake distro whose agent answers git.status with one modified file and records each operation.
	struct StatusDistro
	{
		std::mutex Mutex;
		std::vector<std::string> Operations;
		std::unique_ptr<FakeAgent> Agent;

		Graphite::WslAgents::Connector Connector()
		{
			return [this](const std::string&) -> Core::Result<std::shared_ptr<Core::AgentConnection>> {
				auto agent = std::make_unique<FakeAgent>([this](const Core::AgentRequest& request, FakeAgent& self) {
					{
						std::scoped_lock lock{Mutex};
						Operations.push_back(request.Operation);
					}

					Core::WorkingTreeStatus status;
					status.Entries.push_back(Core::FileStatus{.Path = "changed.txt", .WorkTree = Core::StatusFlags::Modified});
					self.Send(Core::EncodeReply(Core::AgentReply{.Id = request.Id, .Payload = Core::ToJson(status).dump()}));
				});
				std::shared_ptr<Core::AgentConnection> connection{Core::AgentConnection::Open(agent->HostEnd(), "v1")};
				connection->WaitUntilSettled(2s);
				std::scoped_lock lock{Mutex};
				Agent = std::move(agent);
				return connection;
			};
		}
	};
}

TEST_CASE("On a WSL root the port asks the distro's agent for the status")
{
	StatusDistro distro;
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};
	port.SetWslGitFactory([&](const Core::WslSharePath& root, Graphite::WslGit::Reach reach) {
		return std::make_shared<Graphite::WslGit>(agents, root, reach);
	});

	const Core::Result<Core::WorkingTreeStatus> status = port.QueryStatus();

	REQUIRE(status.HasValue());
	REQUIRE(status.GetValue().Entries.size() == 1);
	CHECK(status.GetValue().Entries[0].Path == std::filesystem::path{"changed.txt"});
	std::scoped_lock lock{distro.Mutex};
	CHECK(distro.Operations == std::vector<std::string>{"git.status"});
}

TEST_CASE("On a WSL root what the agent does not answer yet says so, rather than failing on the share")
{
	StatusDistro distro;
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};
	port.SetWslGitFactory([&](const Core::WslSharePath& root, Graphite::WslGit::Reach reach) {
		return std::make_shared<Graphite::WslGit>(agents, root, reach);
	});

	const Core::Result<std::vector<Core::Branch>> branches = port.QueryBranches();

	REQUIRE_FALSE(branches.HasValue());
	CHECK(branches.GetError().Code == Core::ErrorCode::Unsupported);
	CHECK(branches.GetError().Message == "not available for folders inside WSL yet");
}

TEST_CASE("A local root never reaches a WSL agent")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	std::atomic<int> made{0};
	Graphite::RepositoryGitPort port{fixture.GetPath()};
	port.SetWslGitFactory([&](const Core::WslSharePath&, Graphite::WslGit::Reach) -> std::shared_ptr<Graphite::WslGit> {
		++made;
		return nullptr;
	});

	REQUIRE(port.QueryStatus().HasValue());
	REQUIRE(port.QueryBranches().HasValue());

	CHECK(made == 0);
}
