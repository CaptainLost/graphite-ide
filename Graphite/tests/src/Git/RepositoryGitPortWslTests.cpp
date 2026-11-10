#include <doctest/doctest.h>

#include "Git/RepositoryGitPort.h"
#include "Wsl/WslAgents.h"
#include "Wsl/WslGit.h"

#include "FakeAgent.h"
#include "GitFixture.h"
#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Process.h"
#include "Core/Remote/AgentDeployment.h"
#include "Core/Remote/GitCodec.h"
#include "Core/Remote/GraphCodec.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <process.h>
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

	const Core::Result<std::vector<std::string>> changed = port.PathsChangedBetween(port.RepositoryNow(), Core::Oid{}, Core::Oid{});

	REQUIRE_FALSE(changed.HasValue());
	CHECK(changed.GetError().Code == Core::ErrorCode::Unsupported);
	CHECK(changed.GetError().Message == "not available for folders inside WSL yet");
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

	// Per process: another test run at the same time would share the folder.
	const std::filesystem::path folder = std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu\\tmp\\"} / Core::FromUtf8("graphite-port-test-" + std::to_string(_getpid()));
	std::filesystem::remove_all(folder, error);
	std::filesystem::create_directories(folder, error);
	Graphite::WslAgents agents{Graphite::WslAgents::ConnectThroughWsl(agentBinary)};
	TempDirectory store;
	Graphite::RepositoryGitPort port{folder, store.GetPath()};
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

	const Graphite::LineReadInput linesInput{.Branches = Core::CreateRef<const std::vector<Core::Branch>>(branches.GetValue()), .Now = 1800000000};
	const Core::Result<Graphite::LineRead> lines = port.ReadLines(port.RepositoryNow(), linesInput);
	REQUIRE_MESSAGE(lines.HasValue(), (lines ? "" : lines.GetError().Message));
	CHECK(lines.GetValue().Refs.size() == 2);
	REQUIRE(lines.GetValue().Graph != nullptr);
	CHECK(lines.GetValue().Graph->Find(first.GetValue()) != Core::AncestryGraph::kNoCommit);

	const Core::Result<Graphite::HeadBlame> blame = port.BlameAtHead(folder / "a.txt");
	REQUIRE_MESSAGE(blame.HasValue(), (blame ? "" : blame.GetError().Message));
	CHECK(blame.GetValue().Tracked);
	REQUIRE(blame.GetValue().Blame != nullptr);
	CHECK(blame.GetValue().Blame->Commits.contains(first.GetValue()));

	const Core::Result<std::vector<Core::WorktreeEntry>> worktrees = port.QueryWorktrees();
	REQUIRE(worktrees.HasValue());
	REQUIRE(worktrees.GetValue().size() == 1);
	CHECK(Core::NormalizeDirectory(worktrees.GetValue()[0].Path) == Core::NormalizeDirectory(folder));

	const Core::Result<Core::HeadState> initialHead = port.QueryHead();
	REQUIRE(initialHead.HasValue());
	const Core::Result<Graphite::ReviewRange> range =
		port.ResolveRange(Graphite::ReviewKey{.Branch = "refs/heads/feature", .Base = "refs/heads/" + initialHead.GetValue().BranchName});
	REQUIRE(range.HasValue());
	CHECK(range.GetValue().Problem == Graphite::RangeProblem::None);

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

namespace {

	// Answers a lines read of one branch, main, whose tip is kTip with no parents.
	void AnswerLinesRead(const Core::AgentRequest& request, FakeAgent& self)
	{
		if (request.Operation == "git.remotes")
		{
			self.Send(Core::EncodeReply(Core::AgentReply{.Id = request.Id, .Payload = "[]"}));
		}
		else if (request.Operation == "git.shallowBoundary")
		{
			self.Reply(request.Id, {{"ids", nlohmann::json::array()}});
		}
		else if (request.Operation == "git.commonDirectory")
		{
			self.Reply(request.Id, {{"path", "/home/me/p/.git"}});
		}
		else if (request.Operation == "git.graphCommits")
		{
			const std::vector<Core::GraphCommit> commits{Core::GraphCommit{.Id = kTip, .Time = 1700000000, .AuthorKey = "anna@example.com"}};
			self.Reply(request.Id, {{"count", 1}}, Core::EncodeGraphCommits(commits));
		}
		else
		{
			self.Reply(request.Id, {{"held", nlohmann::json::array()}});
		}
	}

	Graphite::LineReadInput OneBranchInput()
	{
		Core::Branch main{.Name = "main", .FullName = "refs/heads/main", .Tip = kTip, .IsHead = true};
		main.TipCommit = Core::CommitSummary{.Id = kTip};
		return Graphite::LineReadInput{.Branches = Core::CreateRef<const std::vector<Core::Branch>>(std::vector<Core::Branch>{main}), .Now = 1700000100};
	}
}

TEST_CASE("On a WSL root the lines are read through the agent, into a store named by its common directory")
{
	AnsweringDistro distro;
	distro.Answer = AnswerLinesRead;
	Graphite::WslAgents agents{distro.Connector()};
	TempDirectory store;
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p", store.GetPath()};
	UseAgents(port, agents);

	const Core::Result<Graphite::LineRead> read = port.ReadLines(port.RepositoryNow(), OneBranchInput());
	const Core::Result<Graphite::LineRead> again = port.ReadLines(port.RepositoryNow(), OneBranchInput());

	REQUIRE_MESSAGE(read.HasValue(), (read ? "" : read.GetError().Message));
	REQUIRE(read.GetValue().Graph != nullptr);
	CHECK(read.GetValue().Graph->Find(kTip) != Core::AncestryGraph::kNoCommit);
	CHECK(read.GetValue().Refs.size() == 1);
	CHECK(read.GetValue().Store.Graph.parent_path().parent_path() == store.GetPath());
	REQUIRE(again.HasValue());
	CHECK(again.GetValue().Store.Graph == read.GetValue().Store.Graph);
	CHECK(again.GetValue().CommitsRead == 0);
}

TEST_CASE("A WSL lines read for a folder no longer open is dropped without asking the agent")
{
	AnsweringDistro distro;
	distro.Answer = AnswerLinesRead;
	Graphite::WslAgents agents{distro.Connector()};
	TempDirectory store;
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p", store.GetPath()};
	UseAgents(port, agents);
	const std::uint64_t asked = port.RepositoryNow();

	port.SetRoot(L"\\\\wsl.localhost\\Ubuntu\\home\\me\\other");
	const Core::Result<Graphite::LineRead> read = port.ReadLines(asked, OneBranchInput());

	REQUIRE_FALSE(read.HasValue());
	CHECK(read.GetError().Code == Core::ErrorCode::Cancelled);
	std::scoped_lock lock{distro.Mutex};
	CHECK(distro.Operations.empty());
}

TEST_CASE("On a WSL root a blame whose HEAD bytes are not text is unusable")
{
	AnsweringDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		const std::string head{"\x00\x01\x02\x03", 4};
		Core::Blame blame;
		blame.At = kTip;
		self.Reply(request.Id, {{"trackable", true}, {"headSize", head.size()}}, head + Core::ToJson(blame).dump());
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};
	UseAgents(port, agents);

	const Core::Result<Graphite::HeadBlame> blame = port.BlameAtHead(L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p\\a.bin");

	REQUIRE(blame.HasValue());
	CHECK(blame.GetValue().Tracked);
	CHECK(blame.GetValue().Unusable);
}

TEST_CASE("On a WSL root a blame of a file in another distro is untracked without asking the agent")
{
	AnsweringDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) { self.Reply(request.Id, {{"trackable", true}, {"headSize", 0}}, "{}"); };
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};
	UseAgents(port, agents);

	const Core::Result<Graphite::HeadBlame> blame = port.BlameAtHead(L"\\\\wsl.localhost\\Debian\\home\\me\\p\\a.txt");

	REQUIRE(blame.HasValue());
	CHECK_FALSE(blame.GetValue().Tracked);
	std::scoped_lock lock{distro.Mutex};
	CHECK(distro.Operations.empty());
}

TEST_CASE("On a WSL root the worktrees come back as share paths")
{
	AnsweringDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		const std::vector<Core::WorktreeEntry> worktrees{Core::WorktreeEntry{.Path = "/home/me/p", .Name = "p", .IsMain = true},
														 Core::WorktreeEntry{.Path = "/home/me/wt", .Name = "wt"}};
		self.Send(Core::EncodeReply(Core::AgentReply{.Id = request.Id, .Payload = Core::ToJsonList(worktrees).dump()}));
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};
	UseAgents(port, agents);

	const Core::Result<std::vector<Core::WorktreeEntry>> worktrees = port.QueryWorktrees();

	REQUIRE(worktrees.HasValue());
	REQUIRE(worktrees.GetValue().size() == 2);
	CHECK(worktrees.GetValue()[0].Path.native() == L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p");
	CHECK(worktrees.GetValue()[1].Path.native() == L"\\\\wsl.localhost\\Ubuntu\\home\\me\\wt");
}

TEST_CASE("On a WSL root a worktree's diff is asked of the worktree itself, exactly")
{
	AnsweringDistro distro;
	nlohmann::json asked;
	distro.Answer = [&](const Core::AgentRequest& request, FakeAgent& self) {
		asked = request.Arguments;
		self.Send(Core::EncodeReply(Core::AgentReply{.Id = request.Id, .Payload = Core::ToJson(Core::Diff{}).dump()}));
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};
	UseAgents(port, agents);

	const Core::Result<Core::Diff> diff = port.QueryWorktreeDiff(L"\\\\wsl.localhost\\Ubuntu\\home\\me\\wt", Core::WhitespaceInDiff::Shown);
	const Core::Result<Core::Diff> elsewhere = port.QueryWorktreeDiff(L"\\\\wsl.localhost\\Debian\\home\\me\\wt", Core::WhitespaceInDiff::Shown);

	REQUIRE(diff.HasValue());
	CHECK(asked.at("root") == "/home/me/wt");
	CHECK(asked.at("reach") == "exactly");
	REQUIRE_FALSE(elsewhere.HasValue());
	CHECK(elsewhere.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("On a WSL root since viewed diffs the agent's two versions here")
{
	AnsweringDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		self.Reply(request.Id, {{"sides", {{{"present", true}, {"size", 4}}, {{"present", true}, {"size", 8}}}}}, "one\none\ntwo\n");
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::RepositoryGitPort port{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};
	UseAgents(port, agents);

	const Core::Result<Graphite::SinceViewedAnswer> answer =
		port.ReadSinceViewed(Graphite::SinceViewedRequest{.ViewedBlob = kTip, .CurrentBlob = kTip, .Path = "a.txt", .Tip = kTip});

	REQUIRE(answer.HasValue());
	CHECK(answer.GetValue().Texts.Old == "one\n");
	CHECK(answer.GetValue().Texts.New == "one\ntwo\n");
	REQUIRE(answer.GetValue().Diff.Hunks.size() == 1);
}
