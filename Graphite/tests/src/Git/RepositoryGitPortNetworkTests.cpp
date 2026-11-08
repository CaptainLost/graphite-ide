#include <doctest/doctest.h>

#include "Git/RepositoryGitPort.h"

#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Command/GitExecutable.h"
#include "Core/Git/Command/GitRunner.h"
#include "Core/Git/Lines/LineModel.h"
#include "Git/Lines/LineReader.h"
#include "Review/ReviewRepository.h"

#include <git2.h>

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The six network operations run through the real port, against local bare repositories named by path: nothing here
// contacts a network. Each test asserts the effect on the repositories, so an argument in the wrong place fails it.

namespace {

	constexpr char kSkippedWithoutGit[] = "Skipped: git is neither on PATH nor where Git for Windows installs it";
	constexpr char kRemote[] = "origin";
	constexpr char kBranch[] = "master";
	constexpr int kHistoryLength = 3;

	void IgnoreLine([[maybe_unused]] std::string_view line)
	{
	}

	void IgnorePercent([[maybe_unused]] int percent)
	{
	}

	std::string Trimmed(std::string text)
	{
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
		{
			text.pop_back();
		}

		return text;
	}

	std::string ForwardSlashes(const std::filesystem::path& path)
	{
		std::string text = Core::ToUtf8(path);

		for (char& character : text)
		{
			if (character == '\\')
			{
				character = '/';
			}
		}

		return text;
	}

	// For as long as it lives, every git this process starts reads no system config and an empty global one, so nothing
	// of the machine's or the user's (init.defaultBranch, push.default, url.insteadOf) changes what a test sees.
	class EmptyGitConfig
	{
	public:
		EmptyGitConfig()
		{
			m_GlobalConfig = m_Directory.GetPath() / "empty-global-gitconfig";
			std::ofstream{m_GlobalConfig};

			m_PreviousNoSystem = Read(L"GIT_CONFIG_NOSYSTEM");
			m_PreviousGlobal = Read(L"GIT_CONFIG_GLOBAL");
			::SetEnvironmentVariableW(L"GIT_CONFIG_NOSYSTEM", L"1");
			::SetEnvironmentVariableW(L"GIT_CONFIG_GLOBAL", m_GlobalConfig.c_str());
		}

		~EmptyGitConfig()
		{
			Restore(L"GIT_CONFIG_NOSYSTEM", m_PreviousNoSystem);
			Restore(L"GIT_CONFIG_GLOBAL", m_PreviousGlobal);
		}

		EmptyGitConfig(const EmptyGitConfig&) = delete;
		EmptyGitConfig& operator=(const EmptyGitConfig&) = delete;

	private:
		static std::optional<std::wstring> Read(const wchar_t* name)
		{
			const DWORD lengthWithTerminator = ::GetEnvironmentVariableW(name, nullptr, 0);

			if (lengthWithTerminator == 0)
			{
				return std::nullopt;
			}

			std::wstring value(lengthWithTerminator, L'\0');
			value.resize(::GetEnvironmentVariableW(name, value.data(), lengthWithTerminator));
			return value;
		}

		static void Restore(const wchar_t* name, const std::optional<std::wstring>& value)
		{
			::SetEnvironmentVariableW(name, value.has_value() ? value->c_str() : nullptr);
		}

		TempDirectory m_Directory;
		std::filesystem::path m_GlobalConfig;
		std::optional<std::wstring> m_PreviousNoSystem;
		std::optional<std::wstring> m_PreviousGlobal;
	};

	// A work repository with a bare remote `origin` of its own and a peer clone of that remote that can move it on.
	class NetworkScene
	{
	public:
		explicit NetworkScene(std::filesystem::path git) : m_Git(std::move(git))
		{
			m_Repository.Write("a.txt", "one\n");
			m_Repository.Commit("one");
			m_Bare = AddBareRemote(kRemote);
		}

		ReviewRepository& Work() { return m_Repository; }
		const std::filesystem::path& WorkPath() const { return m_Repository.GetPath(); }
		const std::filesystem::path& Bare() const { return m_Bare; }

		std::filesystem::path AddBareRemote(const std::string& name)
		{
			const std::filesystem::path bare = m_Remotes.MakeDirectory(name);
			Git(bare, {"init", "--bare", "--initial-branch=master"});
			Git(WorkPath(), {"remote", "add", name, Core::ToUtf8(bare)});
			return bare;
		}

		std::string Git(const std::filesystem::path& directory, std::vector<std::string> arguments) const
		{
			const Core::GitOutput output = GitAnyway(directory, std::move(arguments));
			INFO(output.Errors);
			REQUIRE(output.ExitCode == 0);
			return Trimmed(output.Out);
		}

		Core::GitOutput GitAnyway(const std::filesystem::path& directory, std::vector<std::string> arguments) const
		{
			const Core::GitRun run{.Arguments = std::move(arguments),
								   .WorkingDirectory = directory,
								   .Environment = Core::GitEnvironment(false)};
			const std::atomic<bool> neverCancelled{false};
			Core::Result<Core::GitOutput> finished = Core::RunGit(m_Git, run, neverCancelled, IgnoreLine);
			REQUIRE(finished.HasValue());
			return std::move(finished).GetValue();
		}

		void PublishMaster()
		{
			Git(WorkPath(), {"push", kRemote, kBranch});
			Git(WorkPath(), {"branch", "--set-upstream-to=origin/master", kBranch});
		}

		std::filesystem::path Peer()
		{
			if (m_Peer.empty())
			{
				m_Peer = m_PeerDirectory.MakeDirectory("peer");
				Git(m_PeerDirectory.GetPath(), {"clone", Core::ToUtf8(m_Bare), "peer"});
			}

			return m_Peer;
		}

		void PeerCommitsAndPushes(const std::string& file, const std::string& contents)
		{
			const std::filesystem::path peer = Peer();
			ReviewRepository::WriteFileAt(peer / file, contents);
			Git(peer, {"add", file});
			Git(peer, {"-c", "user.name=Peer", "-c", "user.email=peer@example.com", "commit", "-m", "peer " + file});
			Git(peer, {"push", kRemote, kBranch});
		}

		std::string Head() const { return Git(WorkPath(), {"rev-parse", "HEAD"}); }

	private:
		EmptyGitConfig m_Isolation;
		ReviewRepository m_Repository;
		TempDirectory m_Remotes;
		TempDirectory m_PeerDirectory;
		std::filesystem::path m_Git;
		std::filesystem::path m_Bare;
		std::filesystem::path m_Peer;
	};

	// The port's line read of the branches it lists now, given the graph of the read before (null loads the store's).
	Graphite::LineRead ReadLinesThrough(Graphite::RepositoryGitPort& port, Core::Ref<const Core::AncestryGraph> graph, std::int64_t now)
	{
		Core::Result<std::vector<Core::Branch>> branches = port.QueryBranches();
		REQUIRE(branches.HasValue());
		const Core::Ref<const std::vector<Core::Branch>> snapshot = Core::CreateRef<const std::vector<Core::Branch>>(std::move(branches).GetValue());

		Core::Result<Graphite::LineRead> read =
			port.ReadLines(port.RepositoryNow(), Graphite::LineReadInput{.Branches = snapshot, .Graph = std::move(graph), .Now = now});
		INFO((read.HasValue() ? std::string{} : read.GetError().Message));
		REQUIRE(read.HasValue());
		return std::move(read).GetValue();
	}

	// The state the read gives the branch named `fullName`; empty when it has no line for it.
	std::optional<Core::BranchState> LineStateOf(const Graphite::LineRead& read, const std::string& fullName)
	{
		for (const Core::BranchLine& line : read.Model.Branches)
		{
			if (read.Refs[line.Ref].FullName == fullName)
			{
				return line.State;
			}
		}

		return std::nullopt;
	}

	// Where libgit2 looks for configuration files, pointed at an empty folder for as long as it lives, so that a
	// core.sshCommand of the machine's or the user's cannot reach a test.
	class NoConfigAboveRepository
	{
	public:
		NoConfigAboveRepository()
		{
			git_libgit2_init();
			Point(m_Empty.GetPath());
		}

		~NoConfigAboveRepository()
		{
			Point(nullptr);
			git_libgit2_shutdown();
		}

		NoConfigAboveRepository(const NoConfigAboveRepository&) = delete;
		NoConfigAboveRepository& operator=(const NoConfigAboveRepository&) = delete;

	private:
		static void Point(const std::filesystem::path& folder) { Point(Core::ToUtf8(folder).c_str()); }

		static void Point(const char* folder)
		{
			for (const git_config_level_t level : {GIT_CONFIG_LEVEL_SYSTEM, GIT_CONFIG_LEVEL_XDG, GIT_CONFIG_LEVEL_GLOBAL,
												 GIT_CONFIG_LEVEL_PROGRAMDATA})
			{
				git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, static_cast<int>(level), folder);
			}
		}

		TempDirectory m_Empty;
	};
}

TEST_CASE("Git network port: fetch all brings new commits and prunes a deleted branch")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};
	scene.PublishMaster();
	scene.Git(scene.WorkPath(), {"push", kRemote, "master:topic"});
	scene.Git(scene.WorkPath(), {"fetch", kRemote});
	REQUIRE(scene.GitAnyway(scene.WorkPath(), {"rev-parse", "--verify", "refs/remotes/origin/topic"}).ExitCode == 0);

	scene.PeerCommitsAndPushes("b.txt", "two\n");
	scene.Git(scene.Peer(), {"push", kRemote, ":topic"});

	Graphite::RepositoryGitPort port{scene.WorkPath()};
	const std::atomic<bool> neverCancelled{false};
	const Core::NetworkOutcome fetched = port.FetchAll(neverCancelled, IgnorePercent);

	INFO(fetched.Failure.Copied);
	REQUIRE(fetched.Result == Core::NetworkResult::Done);
	CHECK(scene.Git(scene.WorkPath(), {"rev-parse", "refs/remotes/origin/master"}) == scene.Git(scene.Peer(), {"rev-parse", "HEAD"}));
	CHECK(scene.GitAnyway(scene.WorkPath(), {"rev-parse", "--verify", "refs/remotes/origin/topic"}).ExitCode != 0);
}

TEST_CASE("Git network port: fetch one remote leaves the others as they were")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};
	scene.PublishMaster();
	const std::filesystem::path second = scene.AddBareRemote("second");
	scene.Git(scene.WorkPath(), {"push", "second", kBranch});
	scene.Git(scene.WorkPath(), {"fetch", "--all"});
	const std::string before = scene.Head();

	scene.PeerCommitsAndPushes("b.txt", "two\n");
	scene.Git(scene.Peer(), {"remote", "add", "second", Core::ToUtf8(second)});
	scene.Git(scene.Peer(), {"push", "second", kBranch});

	Graphite::RepositoryGitPort port{scene.WorkPath()};
	const std::atomic<bool> neverCancelled{false};
	const Core::NetworkOutcome fetched = port.FetchRemote(neverCancelled, IgnorePercent, "second");

	INFO(fetched.Failure.Copied);
	REQUIRE(fetched.Result == Core::NetworkResult::Done);
	CHECK(scene.Git(scene.WorkPath(), {"rev-parse", "refs/remotes/second/master"}) == scene.Git(scene.Peer(), {"rev-parse", "HEAD"}));
	CHECK(scene.Git(scene.WorkPath(), {"rev-parse", "refs/remotes/origin/master"}) == before);
}

TEST_CASE("Git network port: fast-forward moves the branch and the work tree to the upstream")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};
	scene.PublishMaster();
	scene.PeerCommitsAndPushes("b.txt", "two\n");
	scene.Git(scene.WorkPath(), {"fetch", kRemote});

	Graphite::RepositoryGitPort port{scene.WorkPath()};
	const std::atomic<bool> neverCancelled{false};
	const Core::NetworkOutcome forwarded = port.FastForward(neverCancelled, IgnorePercent, "refs/remotes/origin/master");

	INFO(forwarded.Failure.Copied);
	REQUIRE(forwarded.Result == Core::NetworkResult::Done);
	CHECK(scene.Head() == scene.Git(scene.Peer(), {"rev-parse", "HEAD"}));
	CHECK(std::filesystem::exists(scene.WorkPath() / "b.txt"));
}

TEST_CASE("Git network port: push sends the branch to its upstream branch")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};
	scene.PublishMaster();
	scene.Work().Write("local.txt", "mine\n");
	scene.Work().Commit("local");

	Graphite::RepositoryGitPort port{scene.WorkPath()};
	const std::atomic<bool> neverCancelled{false};
	const Core::NetworkOutcome pushed = port.PushTo(neverCancelled, IgnorePercent, kRemote, std::string{kBranch});

	INFO(pushed.Failure.Copied);
	REQUIRE(pushed.Result == Core::NetworkResult::Done);
	CHECK(scene.Git(scene.Bare(), {"rev-parse", kBranch}) == scene.Head());
}

TEST_CASE("Git network port: push without an upstream sets one")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};

	Graphite::RepositoryGitPort port{scene.WorkPath()};
	const std::atomic<bool> neverCancelled{false};
	const Core::NetworkOutcome pushed = port.PushTo(neverCancelled, IgnorePercent, kRemote, std::nullopt);

	INFO(pushed.Failure.Copied);
	REQUIRE(pushed.Result == Core::NetworkResult::Done);
	CHECK(scene.Git(scene.Bare(), {"rev-parse", kBranch}) == scene.Head());
	CHECK(scene.Git(scene.WorkPath(), {"config", "branch.master.remote"}) == kRemote);
	CHECK(scene.Git(scene.WorkPath(), {"config", "branch.master.merge"}) == "refs/heads/master");
}

TEST_CASE("Git network port: setting the remote's head creates refs/remotes/<remote>/HEAD")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};
	scene.PublishMaster();

	// Newer gits record the remote's head on a fetch of their own accord; this keeps the test about ours.
	scene.Git(scene.WorkPath(), {"config", "remote.origin.followRemoteHEAD", "never"});
	scene.Git(scene.WorkPath(), {"fetch", kRemote});
	REQUIRE(scene.GitAnyway(scene.WorkPath(), {"symbolic-ref", "refs/remotes/origin/HEAD"}).ExitCode != 0);

	Graphite::RepositoryGitPort port{scene.WorkPath()};
	const std::atomic<bool> neverCancelled{false};
	const Core::NetworkOutcome headSet = port.SetRemoteHeadAuto(neverCancelled, IgnorePercent, kRemote);

	INFO(headSet.Failure.Copied);
	REQUIRE(headSet.Result == Core::NetworkResult::Done);
	CHECK(scene.Git(scene.WorkPath(), {"symbolic-ref", "refs/remotes/origin/HEAD"}) == "refs/remotes/origin/master");
}

TEST_CASE("Git network port: full history deepens a shallow clone")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};

	for (int commitIndex = 1; commitIndex < kHistoryLength; ++commitIndex)
	{
		scene.Work().Write("a.txt", "change " + std::to_string(commitIndex) + "\n");
		scene.Work().Commit("change " + std::to_string(commitIndex));
	}

	TempDirectory cloneDirectory;
	scene.Git(cloneDirectory.GetPath(), {"clone", "--depth", "1", "file:///" + ForwardSlashes(scene.WorkPath()), "shallow"});

	const std::filesystem::path shallow = cloneDirectory.GetPath() / "shallow";
	REQUIRE(scene.Git(shallow, {"rev-list", "--count", "HEAD"}) == "1");

	Graphite::RepositoryGitPort port{shallow};
	const std::atomic<bool> neverCancelled{false};
	const Core::NetworkOutcome deepened = port.FetchFullHistory(neverCancelled, IgnorePercent);

	INFO(deepened.Failure.Copied);
	REQUIRE(deepened.Result == Core::NetworkResult::Done);
	CHECK(scene.Git(shallow, {"rev-list", "--count", "HEAD"}) == std::to_string(kHistoryLength));
}

TEST_CASE("Git network port: once the full history is fetched, the port reads the commits the shallow clone lacked")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};

	for (int commitIndex = 1; commitIndex < kHistoryLength; ++commitIndex)
	{
		scene.Work().Write("a.txt", "change " + std::to_string(commitIndex) + "\n");
		scene.Work().Commit("change " + std::to_string(commitIndex));
	}

	TempDirectory cloneDirectory;
	scene.Git(cloneDirectory.GetPath(), {"clone", "--depth", "1", "file:///" + ForwardSlashes(scene.WorkPath()), "shallow"});
	const std::filesystem::path shallow = cloneDirectory.GetPath() / "shallow";

	// The history read while the clone is shallow, through the port's handle of this thread.
	Graphite::RepositoryGitPort port{shallow};
	const Core::Result<Core::LogPage> before = port.QueryLog(Core::LogRequest{});
	REQUIRE(before.HasValue());
	REQUIRE(before.GetValue().Commits.size() == 1);

	const std::atomic<bool> neverCancelled{false};
	const Core::NetworkOutcome deepened = port.FetchFullHistory(neverCancelled, IgnorePercent);
	INFO(deepened.Failure.Copied);
	REQUIRE(deepened.Result == Core::NetworkResult::Done);

	const Core::Result<Core::LogPage> after = port.QueryLog(Core::LogRequest{});
	REQUIRE(after.HasValue());
	CHECK(after.GetValue().Commits.size() == static_cast<std::size_t>(kHistoryLength));

	port.SetRoot({});
	port.LetGoOfHandlesNoLongerCurrent();
}

TEST_CASE("Git network port: once the full history is fetched, a branch cut off from its line is read as forked from it")
{
	constexpr std::int64_t kNow = 1800000000;

	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	// topic forks from master's first commit; a clone one commit deep has neither side's history down to the fork.
	NetworkScene scene{*git};
	scene.Git(scene.WorkPath(), {"branch", "topic"});
	scene.Work().Write("a.txt", "master's change\n");
	scene.Work().Commit("master's change");
	scene.Git(scene.WorkPath(), {"checkout", "-q", "topic"});
	scene.Work().Write("b.txt", "topic's change\n");
	scene.Work().Commit("topic's change");
	scene.Git(scene.WorkPath(), {"checkout", "-q", kBranch});

	TempDirectory cloneDirectory;
	scene.Git(cloneDirectory.GetPath(),
			  {"clone", "--depth", "1", "--no-single-branch", "file:///" + ForwardSlashes(scene.WorkPath()), "shallow"});
	const std::filesystem::path shallow = cloneDirectory.GetPath() / "shallow";

	TempDirectory storeRoot;
	Graphite::RepositoryGitPort port{shallow, storeRoot.GetPath()};
	const Graphite::LineRead before = ReadLinesThrough(port, nullptr, kNow);
	REQUIRE(LineStateOf(before, "refs/remotes/origin/topic") == std::optional<Core::BranchState>{Core::BranchState::HistoryCutOff});

	const std::atomic<bool> neverCancelled{false};
	const Core::NetworkOutcome deepened = port.FetchFullHistory(neverCancelled, IgnorePercent);
	INFO(deepened.Failure.Copied);
	REQUIRE(deepened.Result == Core::NetworkResult::Done);

	// As the lines model asks it: with the graph of the read before.
	const Graphite::LineRead after = ReadLinesThrough(port, before.Graph, kNow);
	CHECK(LineStateOf(after, "refs/remotes/origin/topic") == std::optional<Core::BranchState>{Core::BranchState::NotMerged});

	port.SetRoot({});
	port.LetGoOfHandlesNoLongerCurrent();
}

TEST_CASE("Git network port: a folder that is not a repository fails with a plain sentence, whether or not git exists")
{
	TempDirectory nowhere;
	Graphite::RepositoryGitPort port{nowhere.GetPath()};
	const std::atomic<bool> neverCancelled{false};

	const Core::NetworkOutcome fetched = port.FetchAll(neverCancelled, IgnorePercent);

	REQUIRE(fetched.Result == Core::NetworkResult::Failed);
	CHECK(fetched.Failure.Shown == "The folder is not a git repository.");
	CHECK(fetched.Failure.Copied == fetched.Failure.Shown);
	CHECK(fetched.Failure.Hint == Core::NetworkHint::None);

	const Graphite::NetworkSetup setup = port.QueryNetworkSetup();
	CHECK_FALSE(setup.Git.has_value());
	CHECK(setup.WorkingDirectory.empty());
}

TEST_CASE("Git network port: a bare repository fails saying it has no work tree")
{
	TempDirectory root;
	git_libgit2_init();
	git_repository* bare = nullptr;
	REQUIRE(git_repository_init(&bare, Core::ToUtf8(root.GetPath()).c_str(), 1) == 0);
	git_repository_free(bare);

	Graphite::RepositoryGitPort port{root.GetPath()};
	const std::atomic<bool> neverCancelled{false};

	const Core::NetworkOutcome pushed = port.PushTo(neverCancelled, IgnorePercent, kRemote, std::nullopt);

	REQUIRE(pushed.Result == Core::NetworkResult::Failed);
	CHECK(pushed.Failure.Shown == "This repository has no work tree.");
	CHECK(pushed.Failure.Copied == pushed.Failure.Shown);
}

TEST_CASE("Git network port: core.sshCommand is reported when set and not when unset, whatever the machine configures")
{
	const NoConfigAboveRepository noConfigAbove;
	ReviewRepository repository;
	repository.Write("a.txt", "one\n");
	repository.Commit("first");
	Graphite::RepositoryGitPort port{repository.GetPath()};

	CHECK_FALSE(port.QueryNetworkSetup().SshCommandConfigured);

	git_repository* opened = nullptr;
	REQUIRE(git_repository_open(&opened, Core::ToUtf8(repository.GetPath()).c_str()) == 0);
	git_config* config = nullptr;
	REQUIRE(git_repository_config(&config, opened) == 0);
	REQUIRE(git_config_set_string(config, "core.sshCommand", "plink -batch") == 0);
	git_config_free(config);
	git_repository_free(opened);

	CHECK(port.QueryNetworkSetup().SshCommandConfigured);
}
