#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Command/GitExecutable.h"
#include "Core/Git/Command/GitNetwork.h"
#include "Core/Git/Command/GitRunner.h"

#include "GitFixture.h"
#include "IsolatedGitConfig.h"
#include "ScopedEnvironmentVariable.h"
#include "TempDirectory.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace {

	constexpr char kSkippedWithoutGit[] = "Skipped: git is neither on PATH nor where Git for Windows installs it";
	constexpr char kRemote[] = "origin";
	constexpr char kBranch[] = "master";
	constexpr char kUpstream[] = "refs/remotes/origin/master";
	constexpr std::chrono::seconds kFreshFetchTolerance{120};
	constexpr int kHistoryLength = 3;

	void IgnoreLine([[maybe_unused]] std::string_view line)
	{
	}

	void IgnorePercent([[maybe_unused]] int percent)
	{
	}

	// Trailing line ends are not part of what a test compares.
	std::string Trimmed(std::string text)
	{
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
		{
			text.pop_back();
		}

		return text;
	}

	// As git's config reads a path on Windows: UTF-8, with forward slashes, which it takes as no escape.
	std::string ForwardSlashes(const std::filesystem::path& path)
	{
		std::string text = Core::ToUtf8(path);
		std::replace(text.begin(), text.end(), '\\', '/');
		return text;
	}

	// The fixture's repository, published to a bare remote of its own, with a peer clone that can move the remote on.
	class NetworkScene
	{
	public:
		explicit NetworkScene(const std::filesystem::path& git) : m_Git(git)
		{
			m_Fixture.Write("a.txt", "one\n");
			m_Fixture.Commit("one");

			m_Bare = m_Fixture.MakeBareRemote(kRemote);
		}

		GitFixture& Fixture() { return m_Fixture; }
		const std::filesystem::path& Bare() const { return m_Bare; }
		const std::filesystem::path& Work() const { return m_Fixture.GetPath(); }

		Core::NetworkRun Run() const { return Core::NetworkRun{.Git = m_Git, .WorkingDirectory = m_Fixture.GetPath()}; }

		// A git command that must succeed; returns its trimmed stdout.
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
			Git(Work(), {"push", kRemote, kBranch});
			Git(Work(), {"branch", "--set-upstream-to=origin/master", kBranch});
		}

		// A second clone of the bare remote, ready to commit from.
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
			WriteFile(peer / file, contents);
			Git(peer, {"add", file});
			Git(peer, {"-c", "user.name=Peer", "-c", "user.email=peer@example.com", "commit", "-m", "peer " + file});
			Git(peer, {"push", kRemote, kBranch});
		}

		std::string Head() const { return Git(Work(), {"rev-parse", "HEAD"}); }

	private:
		static void WriteFile(const std::filesystem::path& path, const std::string& contents)
		{
			std::ofstream file{path, std::ios::binary | std::ios::trunc};
			file << contents;
		}

		IsolatedGitConfig m_Isolation;
		GitFixture m_Fixture;
		TempDirectory m_PeerDirectory;
		std::filesystem::path m_Git;
		std::filesystem::path m_Bare;
		std::filesystem::path m_Peer;
	};
}

TEST_CASE("Network: the hints are read from git's words")
{
	CHECK(Core::HintOf(" ! [rejected]        main -> main (non-fast-forward)") == Core::NetworkHint::Rejected);
	CHECK(Core::HintOf(" ! [rejected]        main -> main (fetch first)") == Core::NetworkHint::Rejected);
	CHECK(Core::HintOf(" ! [rejected]        main -> main (stale info)") == Core::NetworkHint::None);
	CHECK(Core::HintOf("remote: Authentication failed for 'x'") == Core::NetworkHint::SignIn);
	CHECK(Core::HintOf("git@host: Permission denied (publickey).") == Core::NetworkHint::SignIn);
	CHECK(Core::HintOf("Host key verification failed.") == Core::NetworkHint::SignIn);
	CHECK(Core::HintOf("fatal: unable to access") == Core::NetworkHint::None);
}

TEST_CASE("Network: a failure shows the hint first and git's last six lines, and copies all of stderr")
{
	std::string errors;

	for (int line = 1; line <= 9; ++line)
	{
		errors += "line " + std::to_string(line) + "\n";
	}

	errors += "Receiving objects:  45% (9/20)\rReceiving objects: 100% (20/20), done.\n";
	errors += "Host key verification failed.\n";

	const Core::GitOutput output{.ExitCode = 128, .Out = {}, .Errors = errors};
	const Core::NetworkFailure failure = Core::DescribeFailure(output);

	CHECK(failure.Hint == Core::NetworkHint::SignIn);
	CHECK(failure.Copied == errors);
	CHECK(failure.Shown.starts_with("Git could not sign in."));
	CHECK(failure.Shown.ends_with("line 9\nHost key verification failed."));
	CHECK(failure.Shown.find("line 4") == std::string::npos);
	CHECK(failure.Shown.find("Receiving") == std::string::npos);

	const Core::GitOutput plain{.ExitCode = 1, .Out = {}, .Errors = "fatal: no luck\n"};
	CHECK(Core::DescribeFailure(plain).Shown == "fatal: no luck");
	CHECK(Core::DescribeFailure(plain).Hint == Core::NetworkHint::None);
}

TEST_CASE("Network: fetch all brings every remote's new commits and prunes a deleted branch")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};
	scene.PublishMaster();

	const std::filesystem::path second = scene.Fixture().MakeBareRemote("second");
	scene.Git(scene.Work(), {"push", "second", kBranch});
	scene.Git(scene.Work(), {"push", kRemote, "master:topic"});

	const std::atomic<bool> neverCancelled{false};
	REQUIRE(Core::FetchAll(scene.Run(), neverCancelled, IgnorePercent).Result == Core::NetworkResult::Done);
	REQUIRE(scene.GitAnyway(scene.Work(), {"rev-parse", "--verify", "refs/remotes/origin/topic"}).ExitCode == 0);

	scene.PeerCommitsAndPushes("b.txt", "two\n");
	scene.Git(scene.Peer(), {"push", kRemote, ":topic"});
	scene.Git(scene.Peer(), {"remote", "add", "second", Core::ToUtf8(second)});
	scene.Git(scene.Peer(), {"push", "second", kBranch});

	const Core::NetworkOutcome fetched = Core::FetchAll(scene.Run(), neverCancelled, IgnorePercent);
	REQUIRE(fetched.Result == Core::NetworkResult::Done);

	const std::string peerHead = scene.Git(scene.Peer(), {"rev-parse", "HEAD"});
	CHECK(scene.Git(scene.Work(), {"rev-parse", "refs/remotes/origin/master"}) == peerHead);
	CHECK(scene.Git(scene.Work(), {"rev-parse", "refs/remotes/second/master"}) == peerHead);
	CHECK(scene.GitAnyway(scene.Work(), {"rev-parse", "--verify", "refs/remotes/origin/topic"}).ExitCode != 0);
}

TEST_CASE("Network: a fetch acts on the folder's repository whatever repository GIT_DIR names in this process")
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

	GitFixture elsewhere;
	elsewhere.Write("x.txt", "another repository\n");
	elsewhere.Commit("another repository");

	const std::atomic<bool> neverCancelled{false};
	Core::NetworkOutcome fetched;

	{
		// As when Graphite is started from a git hook or alias, which runs with GIT_DIR set.
		const ScopedEnvironmentVariable inheritedGitDir(L"GIT_DIR", (elsewhere.GetPath() / ".git").wstring());
		REQUIRE(inheritedGitDir.WasSet());

		fetched = Core::FetchRemote(scene.Run(), neverCancelled, IgnorePercent, kRemote);
	}

	INFO(fetched.Failure.Copied);
	REQUIRE(fetched.Result == Core::NetworkResult::Done);
	CHECK(scene.Git(scene.Work(), {"rev-parse", kUpstream}) == scene.Git(scene.Peer(), {"rev-parse", "HEAD"}));
	CHECK(scene.GitAnyway(elsewhere.GetPath(), {"rev-parse", "--verify", kUpstream}).ExitCode != 0);
}

TEST_CASE("Network: a fetch leaves a remote with its default branch")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};
	scene.PublishMaster();

	// Newer gits record the remote's default branch on a fetch of their own accord; this keeps the test about ours.
	scene.Git(scene.Work(), {"config", "remote.origin.followRemoteHEAD", "never"});

	const std::atomic<bool> neverCancelled{false};
	REQUIRE(Core::FetchRemote(scene.Run(), neverCancelled, IgnorePercent, kRemote).Result == Core::NetworkResult::Done);
	CHECK(scene.GitAnyway(scene.Work(), {"symbolic-ref", "refs/remotes/origin/HEAD"}).ExitCode != 0);

	REQUIRE(Core::SetRemoteHeadAuto(scene.Run(), neverCancelled, IgnorePercent, kRemote).Result == Core::NetworkResult::Done);
	CHECK(scene.Git(scene.Work(), {"symbolic-ref", "refs/remotes/origin/HEAD"}) == kUpstream);
}

TEST_CASE("Network: fast-forward moves the branch and the work tree to the upstream")
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

	const std::atomic<bool> neverCancelled{false};
	REQUIRE(Core::FetchRemote(scene.Run(), neverCancelled, IgnorePercent, kRemote).Result == Core::NetworkResult::Done);
	REQUIRE(Core::FastForward(scene.Run(), neverCancelled, IgnorePercent, kUpstream).Result == Core::NetworkResult::Done);

	CHECK(scene.Head() == scene.Git(scene.Peer(), {"rev-parse", "HEAD"}));
	CHECK(std::filesystem::exists(scene.Work() / "b.txt"));
}

TEST_CASE("Network: fast-forward refuses a diverged branch and changes nothing")
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

	scene.Fixture().Write("local.txt", "mine\n");
	const std::string localHead = scene.Fixture().Commit("local");

	const std::atomic<bool> neverCancelled{false};
	REQUIRE(Core::FetchRemote(scene.Run(), neverCancelled, IgnorePercent, kRemote).Result == Core::NetworkResult::Done);

	const Core::NetworkOutcome forwarded = Core::FastForward(scene.Run(), neverCancelled, IgnorePercent, kUpstream);

	REQUIRE(forwarded.Result == Core::NetworkResult::Failed);
	CHECK_FALSE(forwarded.Failure.Shown.empty());
	CHECK(scene.Head() == localHead);
	CHECK_FALSE(std::filesystem::exists(scene.Work() / "b.txt"));
}

TEST_CASE("Network: fast-forward refuses to overwrite local changes and keeps them")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};
	scene.PublishMaster();
	const std::string publishedHead = scene.Head();
	scene.PeerCommitsAndPushes("a.txt", "changed by the peer\n");

	scene.Fixture().Write("a.txt", "my unsaved work\n");

	const std::atomic<bool> neverCancelled{false};
	REQUIRE(Core::FetchRemote(scene.Run(), neverCancelled, IgnorePercent, kRemote).Result == Core::NetworkResult::Done);

	const Core::NetworkOutcome forwarded = Core::FastForward(scene.Run(), neverCancelled, IgnorePercent, kUpstream);

	REQUIRE(forwarded.Result == Core::NetworkResult::Failed);
	CHECK(forwarded.Failure.Shown.find("a.txt") != std::string::npos);
	CHECK(scene.Head() == publishedHead);
	CHECK(ReadWithoutCarriageReturns(scene.Work() / "a.txt") == "my unsaved work\n");
}

TEST_CASE("Network: push sends the branch to its upstream")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};
	scene.PublishMaster();

	scene.Fixture().Write("local.txt", "mine\n");
	scene.Fixture().Commit("local");

	const std::atomic<bool> neverCancelled{false};
	REQUIRE(Core::PushTo(scene.Run(), neverCancelled, IgnorePercent, kRemote, std::string{kBranch}).Result == Core::NetworkResult::Done);

	CHECK(scene.Git(scene.Bare(), {"rev-parse", kBranch}) == scene.Head());
}

TEST_CASE("Network: push without an upstream sets one")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};

	const std::atomic<bool> neverCancelled{false};
	REQUIRE(Core::PushTo(scene.Run(), neverCancelled, IgnorePercent, kRemote, std::nullopt).Result == Core::NetworkResult::Done);

	CHECK(scene.Git(scene.Bare(), {"rev-parse", kBranch}) == scene.Head());
	CHECK(scene.Git(scene.Work(), {"config", "branch.master.remote"}) == kRemote);
	CHECK(scene.Git(scene.Work(), {"config", "branch.master.merge"}) == "refs/heads/master");
}

TEST_CASE("Network: push sends no tag, to its upstream or setting one, even where push.followTags is set")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	constexpr std::int64_t kTaggerTime = 1767225600;

	NetworkScene scene{*git};
	scene.PublishMaster();
	scene.Git(scene.Work(), {"config", "push.followTags", "true"});

	scene.Fixture().Write("local.txt", "mine\n");
	const std::string tagged = scene.Fixture().Commit("local");
	scene.Fixture().TagAnnotated("v1", tagged, kTaggerTime, "v1\n");

	const std::atomic<bool> neverCancelled{false};
	REQUIRE(Core::PushTo(scene.Run(), neverCancelled, IgnorePercent, kRemote, std::string{kBranch}).Result == Core::NetworkResult::Done);
	REQUIRE(scene.Git(scene.Bare(), {"rev-parse", kBranch}) == tagged);
	CHECK(scene.Git(scene.Bare(), {"tag", "--list"}).empty());

	scene.Fixture().Branch("side");
	scene.Fixture().Checkout("side");
	scene.Fixture().Write("side.txt", "side\n");
	const std::string sideTagged = scene.Fixture().Commit("side");
	scene.Fixture().TagAnnotated("v2", sideTagged, kTaggerTime, "v2\n");

	REQUIRE(Core::PushTo(scene.Run(), neverCancelled, IgnorePercent, kRemote, std::nullopt).Result == Core::NetworkResult::Done);
	REQUIRE(scene.Git(scene.Bare(), {"rev-parse", "side"}) == sideTagged);
	CHECK(scene.Git(scene.Bare(), {"tag", "--list"}).empty());
}

TEST_CASE("Network: a push the remote rejects as not a fast-forward is a failure with the rejected hint")
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

	scene.Fixture().Write("local.txt", "mine\n");
	scene.Fixture().Commit("local");

	const std::atomic<bool> neverCancelled{false};
	const Core::NetworkOutcome pushed = Core::PushTo(scene.Run(), neverCancelled, IgnorePercent, kRemote, std::string{kBranch});

	REQUIRE(pushed.Result == Core::NetworkResult::Failed);
	CHECK(pushed.Failure.Shown.starts_with("The remote has commits that this branch lacks."));
	CHECK(scene.Git(scene.Bare(), {"rev-parse", kBranch}) == scene.Git(scene.Peer(), {"rev-parse", "HEAD"}));
}

TEST_CASE("Network: a remote that cannot be reached is a plain failure with git's words")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	const IsolatedGitConfig isolation;
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	TempDirectory nowhere;
	fixture.AddRemoteWithUrl(kRemote, Core::ToUtf8(nowhere.GetPath() / "missing"));

	const Core::NetworkRun run{.Git = *git, .WorkingDirectory = fixture.GetPath()};
	const std::atomic<bool> neverCancelled{false};
	const Core::NetworkOutcome fetched = Core::FetchAll(run, neverCancelled, IgnorePercent);

	REQUIRE(fetched.Result == Core::NetworkResult::Failed);
	CHECK(fetched.Failure.Shown.find("fatal") != std::string::npos);
	CHECK(fetched.Failure.Shown.find("Pull first") == std::string::npos);
	CHECK(fetched.Failure.Shown.find("could not sign in") == std::string::npos);
}

TEST_CASE("Network: full history deepens a shallow clone")
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
		scene.Fixture().Write("a.txt", "change " + std::to_string(commitIndex) + "\n");
		scene.Fixture().Commit("change " + std::to_string(commitIndex));
	}

	std::string sourceUrl = "file:///" + Core::ToUtf8(scene.Work());

	for (char& character : sourceUrl)
	{
		if (character == '\\')
		{
			character = '/';
		}
	}

	TempDirectory cloneDirectory;
	scene.Git(cloneDirectory.GetPath(), {"clone", "--depth", "1", sourceUrl, "shallow"});

	const std::filesystem::path shallow = cloneDirectory.GetPath() / "shallow";
	REQUIRE(scene.Git(shallow, {"rev-list", "--count", "HEAD"}) == "1");

	const Core::NetworkRun run{.Git = *git, .WorkingDirectory = shallow};
	const std::atomic<bool> neverCancelled{false};
	REQUIRE(Core::FetchFullHistory(run, neverCancelled, IgnorePercent).Result == Core::NetworkResult::Done);

	CHECK(scene.Git(shallow, {"rev-list", "--count", "HEAD"}) == std::to_string(kHistoryLength));
	CHECK_FALSE(std::filesystem::exists(shallow / ".git" / "shallow"));
}

TEST_CASE("Network: the last fetch time is FETCH_HEAD's")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};
	scene.PublishMaster();

	const std::filesystem::path gitDirectory = scene.Work() / ".git";
	CHECK_FALSE(Core::LastFetchTime(gitDirectory).has_value());

	const std::atomic<bool> neverCancelled{false};
	REQUIRE(Core::FetchAll(scene.Run(), neverCancelled, IgnorePercent).Result == Core::NetworkResult::Done);

	const std::optional<std::chrono::sys_seconds> fetched = Core::LastFetchTime(gitDirectory);
	REQUIRE(fetched.has_value());

	const std::chrono::sys_seconds now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
	CHECK(*fetched <= now + kFreshFetchTolerance);
	CHECK(*fetched >= now - kFreshFetchTolerance);

	const std::filesystem::path fetchHead = gitDirectory / "FETCH_HEAD";
	const std::chrono::sys_seconds expected = std::chrono::floor<std::chrono::seconds>(
		std::chrono::clock_cast<std::chrono::system_clock>(std::filesystem::last_write_time(fetchHead)));
	CHECK(*fetched == expected);
}

TEST_CASE("Network: a remote named like an option is a remote, not an option")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	constexpr char kOptionLikeRemote[] = "-x";

	const IsolatedGitConfig isolation;
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	const std::filesystem::path bare = fixture.MakeBareRemote(kOptionLikeRemote);
	const Core::NetworkRun run{.Git = *git, .WorkingDirectory = fixture.GetPath()};
	const std::atomic<bool> neverCancelled{false};

	const Core::NetworkOutcome firstPush = Core::PushTo(run, neverCancelled, IgnorePercent, kOptionLikeRemote, std::nullopt);
	INFO(firstPush.Failure.Copied);
	REQUIRE(firstPush.Result == Core::NetworkResult::Done);

	fixture.Write("b.txt", "two\n");
	const std::string second = fixture.Commit("two");

	const Core::NetworkOutcome secondPush = Core::PushTo(run, neverCancelled, IgnorePercent, kOptionLikeRemote, std::string{kBranch});
	INFO(secondPush.Failure.Copied);
	REQUIRE(secondPush.Result == Core::NetworkResult::Done);

	const Core::GitRun revParse{.Arguments = {"rev-parse", kBranch},
								.WorkingDirectory = bare,
								.Environment = Core::GitEnvironment(false)};
	Core::Result<Core::GitOutput> pushedHead = Core::RunGit(*git, revParse, neverCancelled, IgnoreLine);
	REQUIRE(pushedHead.HasValue());
	CHECK(Trimmed(pushedHead.GetValue().Out) == second);

	const Core::NetworkOutcome fetched = Core::FetchRemote(run, neverCancelled, IgnorePercent, kOptionLikeRemote);
	INFO(fetched.Failure.Copied);
	CHECK(fetched.Result == Core::NetworkResult::Done);

	const Core::NetworkOutcome headSet = Core::SetRemoteHeadAuto(run, neverCancelled, IgnorePercent, kOptionLikeRemote);
	INFO(headSet.Failure.Copied);
	CHECK(headSet.Result == Core::NetworkResult::Done);
}

TEST_CASE("Network: a failed operation shows the last lines and copies the whole of stderr")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	const IsolatedGitConfig isolation;
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	TempDirectory nowhere;
	fixture.AddRemoteWithUrl(kRemote, Core::ToUtf8(nowhere.GetPath() / "missing"));

	const Core::NetworkRun run{.Git = *git, .WorkingDirectory = fixture.GetPath()};
	const std::atomic<bool> neverCancelled{false};
	const Core::NetworkOutcome fetched = Core::FetchAll(run, neverCancelled, IgnorePercent);

	REQUIRE(fetched.Result == Core::NetworkResult::Failed);
	CHECK(fetched.Failure.Hint == Core::NetworkHint::None);
	CHECK(fetched.Failure.Copied.find("fatal") != std::string::npos);
	CHECK(fetched.Failure.Copied.size() > fetched.Failure.Shown.size());

	const std::size_t lastLineStart = fetched.Failure.Shown.rfind('\n');
	const std::string lastShownLine =
		lastLineStart == std::string::npos ? fetched.Failure.Shown : fetched.Failure.Shown.substr(lastLineStart + 1);
	CHECK(fetched.Failure.Copied.find(lastShownLine) != std::string::npos);
}

TEST_CASE("Network: core.sshCommand counts as set when git reads one, empty or through an include libgit2 cannot follow")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	NetworkScene scene{*git};
	const std::atomic<bool> neverCancelled{false};

	CHECK_FALSE(Core::IsSshCommandConfigured(*git, scene.Work(), neverCancelled));

	scene.Git(scene.Work(), {"config", "core.sshCommand", ""});
	CHECK(Core::IsSshCommandConfigured(*git, scene.Work(), neverCancelled));

	scene.Git(scene.Work(), {"config", "core.sshCommand", "plink -batch"});
	CHECK(Core::IsSshCommandConfigured(*git, scene.Work(), neverCancelled));
	scene.Git(scene.Work(), {"config", "--unset", "core.sshCommand"});
	REQUIRE_FALSE(Core::IsSshCommandConfigured(*git, scene.Work(), neverCancelled));

	// Only for the remotes of one URL, which this repository has; the remote is never contacted.
	TempDirectory includes;
	const std::filesystem::path organisation = includes.GetPath() / "organisation.gitconfig";
	{
		std::ofstream file{organisation, std::ios::binary};
		file << "[core]\n\tsshCommand = ssh -i organisation-key\n";
	}

	scene.Git(scene.Work(), {"remote", "add", "organisation", "file:///nowhere/organisation/repository.git"});
	scene.Git(scene.Work(), {"config", "includeIf.hasconfig:remote.*.url:file:///nowhere/organisation/**.path",
							 ForwardSlashes(organisation)});
	CHECK(Core::IsSshCommandConfigured(*git, scene.Work(), neverCancelled));

	// No answer is not "not set": the user's own transport is kept.
	scene.Git(scene.Work(), {"remote", "remove", "organisation"});
	REQUIRE_FALSE(Core::IsSshCommandConfigured(*git, scene.Work(), neverCancelled));
	const std::atomic<bool> cancelled{true};
	CHECK(Core::IsSshCommandConfigured(*git, scene.Work(), cancelled));
	CHECK(Core::IsSshCommandConfigured(scene.Work() / "no-such-git.exe", scene.Work(), neverCancelled));
}

TEST_CASE("Network: a run cancelled before git exits is cancelled, and a git that cannot start is a failure")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	const IsolatedGitConfig isolation;
	GitFixture fixture;

	const std::atomic<bool> alreadyCancelled{true};
	const Core::NetworkRun real{.Git = *git, .WorkingDirectory = fixture.GetPath()};
	const Core::NetworkOutcome cancelled = Core::FetchAll(real, alreadyCancelled, IgnorePercent);
	CHECK(cancelled.Result == Core::NetworkResult::Cancelled);
	CHECK(cancelled.Failure.Shown.empty());

	TempDirectory nowhere;
	const std::atomic<bool> neverCancelled{false};
	const Core::NetworkRun missing{.Git = nowhere.GetPath() / "no-git.exe", .WorkingDirectory = fixture.GetPath()};
	const Core::NetworkOutcome unstarted = Core::FetchAll(missing, neverCancelled, IgnorePercent);
	REQUIRE(unstarted.Result == Core::NetworkResult::Failed);
	CHECK_FALSE(unstarted.Failure.Shown.empty());
	CHECK(unstarted.Failure.Shown == unstarted.Failure.Copied);
}

TEST_CASE("Network: cancelling a fetch while git runs ends it and everything it started")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

	constexpr std::chrono::seconds kDeadline{30};
	constexpr std::chrono::milliseconds kPoll{10};

	const IsolatedGitConfig isolation;
	TempDirectory root;
	const std::filesystem::path held = root.MakeDirectory("held");
	const std::filesystem::path started = root.GetPath() / "started";

	const std::atomic<bool> neverCancelled{false};
	const std::vector<std::vector<std::string>> setup{
		{"init", "--initial-branch=main", "."},
		{"remote", "add", kRemote, Core::ToUtf8(root.GetPath() / "nowhere")},
	};

	for (const std::vector<std::string>& arguments : setup)
	{
		const Core::GitRun run{.Arguments = arguments, .WorkingDirectory = held, .Environment = Core::GitEnvironment(false)};
		Core::Result<Core::GitOutput> finished = Core::RunGit(*git, run, neverCancelled, IgnoreLine);
		REQUIRE(finished.HasValue());
		REQUIRE(finished.GetValue().ExitCode == 0);
	}

	// The program that stands in for upload-pack writes a flag, then waits on its input, which git never closes.
	std::string flag = Core::ToUtf8(started);

	for (char& character : flag)
	{
		if (character == '\\')
		{
			character = '/';
		}
	}

	const std::string stalledUploadPack = ": > '" + flag + "'; git hash-object --stdin #";
	const Core::GitRun configure{.Arguments = {"config", "remote.origin.uploadpack", stalledUploadPack},
								 .WorkingDirectory = held,
								 .Environment = Core::GitEnvironment(false)};
	Core::Result<Core::GitOutput> configured = Core::RunGit(*git, configure, neverCancelled, IgnoreLine);
	REQUIRE(configured.HasValue());
	REQUIRE(configured.GetValue().ExitCode == 0);

	std::atomic<bool> cancelled{false};
	const Core::NetworkRun run{.Git = *git, .WorkingDirectory = held};
	std::future<Core::NetworkOutcome> fetching = std::async(std::launch::async, Core::FetchAll, std::cref(run), std::cref(cancelled),
												   Core::ProgressSink{IgnorePercent});

	const std::chrono::steady_clock::time_point giveUp = std::chrono::steady_clock::now() + kDeadline;

	while (!std::filesystem::exists(started) && std::chrono::steady_clock::now() < giveUp)
	{
		std::this_thread::sleep_for(kPoll);
	}

	std::error_code renameError;
	const std::filesystem::path moved = root.GetPath() / "moved";
	std::filesystem::rename(held, moved, renameError);
	const bool heldWhileRunning = static_cast<bool>(renameError);
	const bool stillRunning = fetching.wait_for(std::chrono::seconds{0}) != std::future_status::ready;

	cancelled.store(true);

	const bool endedInTime = fetching.wait_for(kDeadline) == std::future_status::ready;

	if (!endedInTime)
	{
		// Nothing else would end it; a test that stalls must not leave git behind.
		FAIL("the cancelled fetch did not end");
	}

	const Core::NetworkOutcome outcome = fetching.get();

	REQUIRE(std::filesystem::exists(started));
	CHECK(stillRunning);
	CHECK(heldWhileRunning);
	CHECK(outcome.Result == Core::NetworkResult::Cancelled);
	CHECK(outcome.Failure.Shown.empty());

	// Every process the fetch started has the directory as its current one until it ends.
	bool released = false;
	const std::chrono::steady_clock::time_point releaseDeadline = std::chrono::steady_clock::now() + kDeadline;

	while (!released && std::chrono::steady_clock::now() < releaseDeadline)
	{
		std::error_code moveError;
		std::filesystem::rename(heldWhileRunning ? held : moved, root.GetPath() / "released", moveError);
		released = !moveError;

		if (!released)
		{
			std::this_thread::sleep_for(kPoll);
		}
	}

	CHECK(released);
}
