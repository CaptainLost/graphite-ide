#include <doctest/doctest.h>

#include "Wsl/WslAgents.h"
#include "Wsl/WslGit.h"

#include "FakeAgent.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Process.h"
#include "Core/Remote/AgentDeployment.h"
#include "Core/Remote/GitCodec.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

using namespace std::chrono_literals;
using AgentFixtures::FakeAgent;

namespace {

	// One fake agent; the test's handler answers each git request.
	struct FakeGitDistro
	{
		FakeAgent::Handler Answer;
		std::mutex Mutex;
		std::unique_ptr<FakeAgent> Agent;
		nlohmann::json LastArguments;

		Graphite::WslAgents::Connector Connector()
		{
			return [this](const std::string&) -> Core::Result<std::shared_ptr<Core::AgentConnection>> {
				auto agent = std::make_unique<FakeAgent>([this](const Core::AgentRequest& request, FakeAgent& self) {
					{
						std::scoped_lock lock{Mutex};
						LastArguments = request.Arguments;
					}
					Answer(request, self);
				});
				std::shared_ptr<Core::AgentConnection> connection{Core::AgentConnection::Open(agent->HostEnd(), "v1")};
				connection->WaitUntilSettled(2s);
				std::scoped_lock lock{Mutex};
				Agent = std::move(agent);
				return connection;
			};
		}
	};

	std::string Dumped(const nlohmann::json& json)
	{
		return json.dump();
	}
}

TEST_CASE("A WSL repository's status comes from its agent, asked with the folder's Linux path")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		Core::WorkingTreeStatus status;
		status.Entries.push_back(Core::FileStatus{.Path = "a b.txt", .WorkTree = Core::StatusFlags::Modified});
		self.Send(Core::EncodeReply(Core::AgentReply{.Id = request.Id, .Payload = Dumped(Core::ToJson(status))}));
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git{agents, Core::WslSharePath{"Ubuntu", "/home/me/p"}, Graphite::WslGit::Reach::SearchUpward};

	const Core::Result<Core::WorkingTreeStatus> status = git.QueryStatus();

	REQUIRE(status.HasValue());
	REQUIRE(status.GetValue().Entries.size() == 1);
	CHECK(status.GetValue().Entries[0].Path == std::filesystem::path{"a b.txt"});
	std::scoped_lock lock{distro.Mutex};
	CHECK(distro.LastArguments.at("root") == "/home/me/p");
	CHECK(distro.LastArguments.at("reach") == "up");
}

TEST_CASE("A WSL repository's working directory comes back as the share path the host names it by")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) { self.Reply(request.Id, {{"path", "/home/me/p"}}); };
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git{agents, Core::WslSharePath{"Ubuntu", "/home/me/p/sub"}, Graphite::WslGit::Reach::SearchUpward};

	const Core::Result<std::filesystem::path> directory = git.GetWorkingDirectory();

	REQUIRE(directory.HasValue());
	CHECK(directory.GetValue().native() == L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p");
}

TEST_CASE("The bases of a share path are asked for by its Linux path, and arrive as HEAD and index text")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		self.Reply(request.Id, {{"trackable", true}, {"sides", {{{"present", true}, {"size", 4}}, {{"present", false}, {"size", 0}}}}}, "head");
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git{agents, Core::WslSharePath{"Ubuntu", "/home/me/p"}, Graphite::WslGit::Reach::SearchUpward};

	const Core::Result<Graphite::WslGit::Bases> bases = git.ReadBases(L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p\\a.txt");

	REQUIRE(bases.HasValue());
	CHECK(bases.GetValue().Trackable);
	CHECK(bases.GetValue().Head == std::optional<std::string>{"head"});
	CHECK_FALSE(bases.GetValue().Index.has_value());
	std::scoped_lock lock{distro.Mutex};
	CHECK(distro.LastArguments.at("path") == "/home/me/p/a.txt");
}

TEST_CASE("An agent that stops during a commit fails the commit with the reason")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest&, FakeAgent& self) { self.Hang(); };
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git{agents, Core::WslSharePath{"Ubuntu", "/home/me/p"}, Graphite::WslGit::Reach::SearchUpward};

	const Core::Result<Core::Oid> committed = git.Commit(Core::CommitRequest{.Message = "x"});

	REQUIRE_FALSE(committed.HasValue());
	CHECK(committed.GetError().Message == "the WSL agent stopped");
}

// The real agent in Ubuntu: init, a file written through the share, status, stage, commit,
// HEAD. Skipped, saying why, without WSL, an Ubuntu distro or the Linux build of the agent.
TEST_CASE("A repository inside WSL is created, changed, staged and committed through the real agent")
{
	const Core::Result<std::filesystem::path> here = Core::ExecutableDirectory();
	REQUIRE(here.HasValue());
#ifdef CORE_DEBUG
	const std::filesystem::path agentBinary = here.GetValue().parent_path().parent_path() / "Debug-linux-x86_64" / "GraphiteAgent" / "graphite-agent";
#else
	const std::filesystem::path agentBinary = here.GetValue().parent_path().parent_path() / "Release-linux-x86_64" / "GraphiteAgent" / "graphite-agent";
#endif
	std::error_code error;

	if (!Core::IsWslAvailable() || !std::filesystem::exists(agentBinary, error) || !std::filesystem::exists(L"\\\\wsl.localhost\\Ubuntu\\tmp", error))
	{
		MESSAGE("no WSL Ubuntu or no Linux build of graphite-agent here: skipped");
		return;
	}

	const std::filesystem::path folder{L"\\\\wsl.localhost\\Ubuntu\\tmp\\graphite-wslgit-test"};
	std::filesystem::remove_all(folder, error);
	std::filesystem::create_directories(folder, error);

	Graphite::WslAgents agents{Graphite::WslAgents::ConnectThroughWsl(agentBinary)};
	Graphite::WslGit git{agents, Core::WslSharePath{"Ubuntu", "/tmp/graphite-wslgit-test"}, Graphite::WslGit::Reach::SearchUpward};

	REQUIRE(git.Init().HasValue());

	// The repository's own identity, so the commit does not depend on the distro's git config.
	std::ofstream{folder / ".git" / "config", std::ios::app} << "[user]\n\tname = Test\n\temail = test@example.com\n";
	std::ofstream{folder / "hello.txt"} << "hello\n";

	const Core::Result<Core::WorkingTreeStatus> status = git.QueryStatus();
	REQUIRE(status.HasValue());
	REQUIRE(status.GetValue().Entries.size() == 1);
	CHECK(status.GetValue().Entries[0].IsUntracked);

	REQUIRE(git.Stage({"hello.txt"}).HasValue());
	const Core::Result<Core::Oid> committed = git.Commit(Core::CommitRequest{.Message = "first"});
	REQUIRE_MESSAGE(committed.HasValue(), (committed ? "" : committed.GetError().Message));

	const Core::Result<Core::HeadState> head = git.QueryHead();
	REQUIRE(head.HasValue());
	CHECK(head.GetValue().Commit == std::optional{committed.GetValue()});

	std::filesystem::remove_all(folder, error);
}
