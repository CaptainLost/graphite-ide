#include <doctest/doctest.h>

#include "Wsl/WslAgents.h"
#include "Wsl/WslGit.h"

#include "FakeAgent.h"

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
#include <mutex>
#include <string>
#include <unordered_set>

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

TEST_CASE("A file in another distro has no bases here, and its distro's path is not asked of this one")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) { self.Reply(request.Id, {{"trackable", true}, {"sides", nlohmann::json::array()}}); };
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git{agents, Core::WslSharePath{"Ubuntu", "/home/me/p"}, Graphite::WslGit::Reach::SearchUpward};

	const Core::Result<Graphite::WslGit::Bases> bases = git.ReadBases(L"\\\\wsl.localhost\\Debian\\home\\me\\p\\a.txt");

	REQUIRE(bases.HasValue());
	CHECK_FALSE(bases.GetValue().Trackable);
	std::scoped_lock lock{distro.Mutex};
	CHECK(distro.LastArguments.is_null());
}

TEST_CASE("A bare repository's working directory is empty, not the distro's root")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) { self.Reply(request.Id, {{"path", ""}}); };
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git{agents, Core::WslSharePath{"Ubuntu", "/srv/repo.git"}, Graphite::WslGit::Reach::SearchUpward};

	const Core::Result<std::filesystem::path> directory = git.GetWorkingDirectory();

	REQUIRE(directory.HasValue());
	CHECK(directory.GetValue().empty());
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

namespace {

	const Core::Oid kSomeCommit = *Core::Oid::FromHex("0123456789abcdef0123456789abcdef01234567");

	Graphite::WslGit GitAt(Graphite::WslAgents& agents)
	{
		return Graphite::WslGit{agents, Core::WslSharePath{"Ubuntu", "/home/me/p"}, Graphite::WslGit::Reach::SearchUpward};
	}
}

TEST_CASE("A WSL repository's branches come from its agent")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		CHECK(request.Operation == "git.branches");
		const std::vector<Core::Branch> branches{Core::Branch{.Name = "main", .FullName = "refs/heads/main", .Tip = kSomeCommit, .IsHead = true}};
		self.Send(Core::EncodeReply(Core::AgentReply{.Id = request.Id, .Payload = Dumped(Core::ToJsonList(branches))}));
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git = GitAt(agents);

	const Core::Result<std::vector<Core::Branch>> branches = git.QueryBranches();

	REQUIRE(branches.HasValue());
	REQUIRE(branches.GetValue().size() == 1);
	CHECK(branches.GetValue()[0].Name == "main");
	CHECK(branches.GetValue()[0].IsHead);
}

TEST_CASE("A log request reaches the agent whole")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		self.Send(Core::EncodeReply(Core::AgentReply{.Id = request.Id, .Payload = Dumped(Core::ToJson(Core::LogPage{}))}));
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git = GitAt(agents);
	Core::LogRequest request;
	request.File = std::filesystem::path{"dir/a.txt"};
	request.Lines = Core::OneBasedLineRange{.First = 2, .Last = 4};
	request.Exclude = kSomeCommit;

	REQUIRE(git.QueryLog(request).HasValue());

	std::scoped_lock lock{distro.Mutex};
	const Core::Result<Core::LogRequest> sent = Core::LogRequestFromJson(distro.LastArguments.at("request"));
	REQUIRE(sent.HasValue());
	CHECK(sent.GetValue().File == request.File);
	CHECK(sent.GetValue().Lines->Last == 4);
	CHECK(sent.GetValue().Exclude == request.Exclude);
}

TEST_CASE("A commit's texts are the agent's new and old sides")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		self.Reply(request.Id, {{"sides", {{{"present", true}, {"size", 3}}, {{"present", false}, {"size", 0}}}}}, "new");
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git = GitAt(agents);

	const auto sides = git.ReadCommitSides(kSomeCommit, "a.txt", "b.txt");

	REQUIRE(sides.HasValue());
	CHECK(sides.GetValue().first == std::optional<std::string>{"new"});
	CHECK_FALSE(sides.GetValue().second.has_value());
	std::scoped_lock lock{distro.Mutex};
	CHECK(distro.LastArguments.at("commit") == kSomeCommit.ToHex());
	CHECK(distro.LastArguments.at("oldPath") == "a.txt");
	CHECK(distro.LastArguments.at("newPath") == "b.txt");
}

TEST_CASE("A checkout the agent could not finish keeps the paths it changed")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		const Core::CheckoutOutcome outcome{.Changed = {"a.txt"}, .Failure = Core::Error{Core::ErrorCode::IoFailure, "disk full"}};
		self.Send(Core::EncodeReply(Core::AgentReply{.Id = request.Id, .Payload = Dumped(Core::ToJson(outcome))}));
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git = GitAt(agents);

	const Core::CheckoutOutcome outcome = git.Checkout(Core::CheckoutTarget{.Name = "feature"});

	REQUIRE(outcome.Changed.size() == 1);
	CHECK(outcome.Changed[0] == std::filesystem::path{"a.txt"});
	REQUIRE(outcome.Failure.has_value());
	CHECK(outcome.Failure->Message == "disk full");
}

TEST_CASE("A checkout the agent never answered fails with the reason and names no changed path")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest&, FakeAgent& self) { self.Hang(); };
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git = GitAt(agents);

	const Core::CheckoutOutcome outcome = git.Checkout(Core::CheckoutTarget{.Name = "feature"});

	CHECK(outcome.Changed.empty());
	REQUIRE(outcome.Failure.has_value());
	CHECK(outcome.Failure->Message == "the WSL agent stopped");
}

TEST_CASE("The common directory comes back in the share spelling")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) { self.Reply(request.Id, {{"path", "/home/me/p/.git"}}); };
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git = GitAt(agents);

	const Core::Result<std::filesystem::path> common = git.GetCommonDirectory();

	REQUIRE(common.HasValue());
	CHECK(common.GetValue().native() == L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p\\.git");
}

TEST_CASE("Ignore answers arrive in the entries' order, and the folder is asked by its Linux path")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		CHECK(request.Operation == "git.ignored");
		self.Reply(request.Id, {{"ignored", {true, false}}});
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git = GitAt(agents);
	const std::vector<Core::DirectoryEntry> entries{
		Core::DirectoryEntry{.Path = L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p\\build", .Name = "build", .IsDirectory = true},
		Core::DirectoryEntry{.Path = L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p\\a.txt", .Name = "a.txt"},
	};

	const Core::Result<std::vector<bool>> ignored = git.QueryIgnored(L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p", entries, 2s);

	REQUIRE(ignored.HasValue());
	CHECK(ignored.GetValue() == std::vector<bool>{true, false});
	std::scoped_lock lock{distro.Mutex};
	CHECK(distro.LastArguments.at("folder") == "/home/me/p");
	CHECK(distro.LastArguments.at("entries")[0].at("name") == "build");
	CHECK(distro.LastArguments.at("entries")[0].at("directory") == true);
}

TEST_CASE("The ignore setup's working directory and rule files come back in the share spelling")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		self.Reply(request.Id, {{"workingDirectory", "/home/me/p"}, {"ruleFiles", {"/home/me/.gitignore", "/home/me/p/.git/info/exclude"}}});
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git = GitAt(agents);

	const Core::Result<Graphite::WslGit::IgnoreSetup> setup = git.QueryIgnoreSetup(2s);

	REQUIRE(setup.HasValue());
	CHECK(setup.GetValue().WorkingDirectory.native() == L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p");
	REQUIRE(setup.GetValue().RuleFiles.size() == 2);
	CHECK(setup.GetValue().RuleFiles[0].native() == L"\\\\wsl.localhost\\Ubuntu\\home\\me\\.gitignore");
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

TEST_CASE("The common directory is asked of the agent once, as it never changes for a root")
{
	FakeGitDistro distro;
	std::atomic<int> asked{0};
	distro.Answer = [&](const Core::AgentRequest& request, FakeAgent& self) {
		++asked;
		self.Reply(request.Id, {{"path", "/home/me/p/.git"}});
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git = GitAt(agents);

	REQUIRE(git.GetCommonDirectory().HasValue());
	REQUIRE(git.GetCommonDirectory().HasValue());

	CHECK(asked == 1);
}

TEST_CASE("Graph commits arrive decoded, the tips and known commits sent as hex")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		const std::vector<Core::GraphCommit> commits{Core::GraphCommit{.Id = kSomeCommit, .Time = 7, .AuthorKey = "anna@example.com"}};
		self.Reply(request.Id, {{"count", 1}}, Core::EncodeGraphCommits(commits));
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git = GitAt(agents);

	const Core::Result<std::vector<Core::GraphCommit>> commits = git.ReadGraphCommits(Core::GraphReadRequest{.Tips = {kSomeCommit}, .Known = {}});

	REQUIRE(commits.HasValue());
	REQUIRE(commits.GetValue().size() == 1);
	CHECK(commits.GetValue()[0].AuthorKey == "anna@example.com");
	std::scoped_lock lock{distro.Mutex};
	CHECK(distro.LastArguments.at("tips") == nlohmann::json::array({kSomeCommit.ToHex()}));
	CHECK(distro.LastArguments.at("known") == nlohmann::json::array());
}

TEST_CASE("Held commits and the shallow boundary come from the agent")
{
	FakeGitDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		if (request.Operation == "git.holdsCommits")
		{
			self.Reply(request.Id, {{"held", {true, false}}});
		}
		else
		{
			self.Reply(request.Id, {{"ids", {kSomeCommit.ToHex()}}});
		}
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslGit git = GitAt(agents);
	Core::Oid other;
	other.Bytes.fill(0x11);

	const Core::Result<std::vector<bool>> held = git.HoldsCommits({kSomeCommit, other});
	const Core::Result<std::unordered_set<Core::Oid>> boundary = git.ShallowBoundary();

	REQUIRE(held.HasValue());
	CHECK(held.GetValue() == std::vector<bool>{true, false});
	REQUIRE(boundary.HasValue());
	CHECK(boundary.GetValue() == std::unordered_set<Core::Oid>{kSomeCommit});
}
