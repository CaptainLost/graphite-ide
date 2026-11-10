#include <doctest/doctest.h>

#include "Git/RepositoryGitPort.h"
#include "Wsl/WslAgents.h"
#include "Wsl/WslGit.h"

#include "FakeAgent.h"
#include "GitFixture.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Process.h"
#include "Core/Remote/AgentDeployment.h"
#include "Core/Remote/GitCodec.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
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

	const Core::Result<std::vector<Core::WorktreeEntry>> worktrees = port.QueryWorktrees();

	REQUIRE_FALSE(worktrees.HasValue());
	CHECK(worktrees.GetError().Code == Core::ErrorCode::Unsupported);
	CHECK(worktrees.GetError().Message == "not available for folders inside WSL yet");
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

namespace {

	// A fake distro whose agent answers each git operation with the test's handler.
	struct AnsweringDistro
	{
		FakeAgent::Handler Answer;
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

	void UseAgents(Graphite::RepositoryGitPort& port, Graphite::WslAgents& agents)
	{
		port.SetWslGitFactory([&](const Core::WslSharePath& root, Graphite::WslGit::Reach reach) {
			return std::make_shared<Graphite::WslGit>(agents, root, reach);
		});
	}

	const Core::Oid kTip = *Core::Oid::FromHex("0123456789abcdef0123456789abcdef01234567");
}

TEST_CASE("On a WSL root the port asks the agent for the branches")
{
	AnsweringDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		const std::vector<Core::Branch> branches{Core::Branch{.Name = "main", .FullName = "refs/heads/main", .Tip = kTip}};
		self.Send(Core::EncodeReply(Core::AgentReply{.Id = request.Id, .Payload = Core::ToJsonList(branches).dump()}));
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};
	UseAgents(port, agents);

	const Core::Result<std::vector<Core::Branch>> branches = port.QueryBranches();

	REQUIRE(branches.HasValue());
	REQUIRE(branches.GetValue().size() == 1);
	CHECK(branches.GetValue()[0].Tip == kTip);
	std::scoped_lock lock{distro.Mutex};
	CHECK(distro.Operations == std::vector<std::string>{"git.branches"});
}

TEST_CASE("On a WSL root a commit's texts are the agent's sides, an absent old side drawn as added")
{
	AnsweringDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		self.Reply(request.Id, {{"sides", {{{"present", true}, {"size", 4}}, {{"present", false}, {"size", 0}}}}}, "new\n");
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};
	UseAgents(port, agents);

	const Core::Result<Graphite::FileTexts> texts = port.ReadFileTexts(kTip, "a.txt", "a.txt");

	REQUIRE(texts.HasValue());
	CHECK(texts.GetValue().HasNew);
	CHECK(texts.GetValue().New == "new\n");
	CHECK_FALSE(texts.GetValue().HasOld);
}

TEST_CASE("On a WSL root a commit count of a known tip is the known count, and a new tip is counted by the agent")
{
	AnsweringDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		if (request.Arguments.contains("knownTip"))
		{
			self.Reply(request.Id, {{"tip", kTip.ToHex()}});
		}
		else
		{
			self.Reply(request.Id, {{"tip", kTip.ToHex()}, {"count", 12}});
		}
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};
	UseAgents(port, agents);

	const Core::Result<Graphite::CommitCount> fresh = port.CountCommits(std::nullopt, Core::FollowedParents::All, std::nullopt);
	const Core::Result<Graphite::CommitCount> known =
		port.CountCommits(std::nullopt, Core::FollowedParents::All, Graphite::CommitCount{.Tip = kTip, .Count = 99});

	REQUIRE(fresh.HasValue());
	CHECK(fresh.GetValue().Count == 12);
	REQUIRE(known.HasValue());
	CHECK(known.GetValue().Count == 99);
}

TEST_CASE("On a WSL root a checkout's failure reaches the caller with the paths it changed")
{
	AnsweringDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		const Core::CheckoutOutcome outcome{.Changed = {"a.txt"}, .Failure = Core::Error{Core::ErrorCode::IoFailure, "disk full"}};
		self.Send(Core::EncodeReply(Core::AgentReply{.Id = request.Id, .Payload = Core::ToJson(outcome).dump()}));
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};
	UseAgents(port, agents);

	const Core::CheckoutOutcome outcome = port.Checkout(Core::CheckoutTarget{.Name = "feature"});

	CHECK(outcome.Changed == std::vector<std::filesystem::path>{"a.txt"});
	REQUIRE(outcome.Failure.has_value());
	CHECK(outcome.Failure->Message == "disk full");
}

TEST_CASE("On a WSL root a stash is saved and dropped through the agent")
{
	AnsweringDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		self.Reply(request.Id, request.Operation == "git.stashSave" ? nlohmann::json{{"saved", true}} : nlohmann::json::object());
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};
	UseAgents(port, agents);

	const Core::Result<bool> saved = port.StashSave("keep", Core::UntrackedFiles::Excluded);
	const Core::Result<void> dropped = port.StashDrop(kTip);

	REQUIRE(saved.HasValue());
	CHECK(saved.GetValue());
	CHECK(dropped.HasValue());
	std::scoped_lock lock{distro.Mutex};
	CHECK(distro.Operations == std::vector<std::string>{"git.stashSave", "git.stashDrop"});
}

// The real agent in Ubuntu behind the port: a commit, a branch, the log, a stash, a checkout and
// the last fetch read over the share. Skipped, saying why, without WSL, an Ubuntu distro or the
// Linux build of the agent.
TEST_CASE("A WSL repository's history, branches, stashes and checkout work through the real agent")
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

	const std::filesystem::path folder{L"\\\\wsl.localhost\\Ubuntu\\tmp\\graphite-port-3b-test"};
	std::filesystem::remove_all(folder, error);
	std::filesystem::create_directories(folder, error);
	Graphite::WslAgents agents{Graphite::WslAgents::ConnectThroughWsl(agentBinary)};
	Graphite::RepositoryGitPort port{folder};
	UseAgents(port, agents);

	REQUIRE(port.Init().HasValue());
	std::ofstream{folder / ".git" / "config", std::ios::app} << "[user]\n\tname = Test\n\temail = test@example.com\n";
	std::ofstream{folder / "a.txt"} << "one\n";
	REQUIRE(port.Stage({"a.txt"}).HasValue());
	const Core::Result<Core::Oid> first = port.Commit(Core::CommitRequest{.Message = "first"});
	REQUIRE_MESSAGE(first.HasValue(), (first ? "" : first.GetError().Message));

	REQUIRE(port.CreateBranch("feature", first.GetValue()).HasValue());
	const Core::Result<std::vector<Core::Branch>> branches = port.QueryBranches();
	REQUIRE(branches.HasValue());
	CHECK(branches.GetValue().size() == 2);

	const Core::Result<Core::LogPage> log = port.QueryLog(Core::LogRequest{});
	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);
	CHECK(log.GetValue().Commits[0].Summary == "first");

	CHECK_FALSE(port.QueryLastFetch().has_value());
	std::ofstream{folder / ".git" / "FETCH_HEAD"} << "\n";
	CHECK(port.QueryLastFetch().has_value());

	std::ofstream{folder / "a.txt"} << "two\n";
	const Core::Result<bool> stashed = port.StashSave("keep", Core::UntrackedFiles::Excluded);
	REQUIRE_MESSAGE(stashed.HasValue(), (stashed ? "" : stashed.GetError().Message));
	const Core::Result<std::vector<Core::StashEntry>> stashes = port.QueryStashes();
	REQUIRE(stashes.HasValue());
	REQUIRE(stashes.GetValue().size() == 1);
	REQUIRE(port.StashDrop(stashes.GetValue()[0].Id).HasValue());

	const Core::CheckoutOutcome checkout = port.Checkout(Core::CheckoutTarget{.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"});
	CHECK_FALSE(checkout.Failure.has_value());
	const Core::Result<Core::HeadState> head = port.QueryHead();
	REQUIRE(head.HasValue());
	CHECK(head.GetValue().BranchName == "feature");

	std::filesystem::remove_all(folder, error);
}
