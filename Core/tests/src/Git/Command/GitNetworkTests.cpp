#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Command/GitExecutable.h"
#include "Core/Git/Command/GitNetwork.h"
#include "Core/Git/Command/GitRunner.h"

#include "GitFixture.h"
#include "TempDirectory.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
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

TEST_CASE("Network: a failed operation shows the last lines and copies the whole of stderr")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

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

TEST_CASE("Network: a run cancelled before git exits is cancelled, and a git that cannot start is a failure")
{
	const std::optional<std::filesystem::path> git = Core::FindGit();

	if (!git.has_value())
	{
		MESSAGE(kSkippedWithoutGit);
		return;
	}

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
