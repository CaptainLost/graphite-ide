#include <doctest/doctest.h>

#include "Git/Network/NetworkText.h"

#include "Core/Git/Command/GitNetwork.h"

#include <cstddef>
#include <optional>
#include <string>

using Graphite::NetworkDone;
using Graphite::NetworkNames;
using Graphite::NetworkOperation;
using Graphite::NetworkState;

namespace {

	const std::string kEllipsis = "\xE2\x80\xA6";

	constexpr char kCoreRejectedSentence[] = "The remote has commits that this branch lacks. Pull first, or merge in a terminal.";
	constexpr char kCoreSignInSentence[] =
		"Git could not sign in. Run `git fetch` once in a terminal to enter credentials or accept the host key, or add your "
		"key to ssh-agent.";

	Core::NetworkFailure FailureWithHint(Core::NetworkHint hint, const std::string& sentence, const std::string& gitLines)
	{
		return Core::NetworkFailure{.Shown = sentence + "\n" + gitLines, .Copied = "the whole of stderr", .Hint = hint};
	}

	std::string LocalChangesStderr(std::size_t fileCount)
	{
		std::string errors = "error: Your local changes to the following files would be overwritten by merge:\n";

		for (std::size_t file = 1; file <= fileCount; ++file)
		{
			errors += "\tfile" + std::to_string(file) + ".txt\n";
		}

		return errors + "Please commit your changes or stash them before you merge.\nAborting\n";
	}
}

TEST_CASE("Network text: the running and done texts and the cancelled ones")
{
	CHECK(Graphite::RunningText(NetworkState{.Running = NetworkOperation::Fetch, .Percent = 45}) == "Fetching" + kEllipsis + " 45%");
	CHECK(Graphite::RunningText(NetworkState{.Running = NetworkOperation::Fetch}) == "Fetching" + kEllipsis);
	CHECK(Graphite::RunningText(NetworkState{.Running = NetworkOperation::FullHistory, .Percent = 7}) ==
		  "Fetching full history" + kEllipsis + " 7%");
	CHECK(Graphite::RunningText(NetworkState{.Running = NetworkOperation::FullHistory}) == "Fetching full history" + kEllipsis);
	CHECK(Graphite::RunningText(NetworkState{.Running = NetworkOperation::Pull}) == "Pulling" + kEllipsis);
	CHECK(Graphite::RunningText(NetworkState{.Running = NetworkOperation::Push, .Percent = 100}) == "Pushing" + kEllipsis + " 100%");
	CHECK(Graphite::RunningText(NetworkState{}).empty());

	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::Fetch}) == "Fetched");
	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::FullHistory}) == "Fetched");
	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::Pull, .Commits = 5}) == "Pulled 5 commits");
	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::Pull, .Commits = 1}) == "Pulled 1 commit");
	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::Pull, .Commits = 1200}) == "Pulled 1,200 commits");
	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::Pull}) == "Pulled");
	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::Push, .Commits = 2}) == "Pushed 2 commits");
	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::Push}) == "Pushed");

	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::Fetch, .Cancelled = true}) == "Fetch cancelled");
	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::FullHistory, .Cancelled = true}) == "Fetch cancelled");
	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::Pull, .Cancelled = true}) == "Pull cancelled");
	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::Push, .Cancelled = true}) == "Push cancelled");

	CHECK(Graphite::FailureTitle(NetworkOperation::Fetch) == "Fetch failed");
	CHECK(Graphite::FailureTitle(NetworkOperation::FullHistory) == "Fetch failed");
	CHECK(Graphite::FailureTitle(NetworkOperation::Pull) == "Pull failed");
	CHECK(Graphite::FailureTitle(NetworkOperation::Push) == "Push failed");
}

TEST_CASE("Network text: the sentences said when an operation cannot go on name the branch and its upstream")
{
	CHECK(Graphite::DivergedText("main", "origin/main", 3, 5) ==
		  "main and origin/main have diverged (3 to push, 5 to pull). Pull can only fast-forward for now; merge or rebase in a "
		  "terminal.");
	CHECK(Graphite::NoUpstreamText("main") == "main has no upstream branch.");
	CHECK(Graphite::NoRemoteText() == "This repository has no remote.");
	CHECK(Graphite::NoLongerCheckedOutText("main", "origin/main") ==
		  "main is no longer checked out, so Pull did not merge origin/main.");
}

TEST_CASE("Network text: a rejected push names the remote and the branch in place of the general sentence")
{
	const Core::NetworkFailure failure =
		FailureWithHint(Core::NetworkHint::Rejected, kCoreRejectedSentence, " ! [rejected]        main -> main (fetch first)");

	CHECK(Graphite::FailureMessage(NetworkOperation::Push, failure, NetworkNames{.Remote = "origin", .Branch = "main"}) ==
		  "origin has commits that main lacks. Pull first, or merge in a terminal.\n ! [rejected]        main -> main (fetch first)");

	CHECK(Graphite::FailureMessage(NetworkOperation::Push, failure, NetworkNames{}) == failure.Shown);
}

TEST_CASE("Network text: a failed sign-in names the remote when it is known and keeps the general sentence when not")
{
	const Core::NetworkFailure failure =
		FailureWithHint(Core::NetworkHint::SignIn, kCoreSignInSentence, "git@example.com: Permission denied (publickey).");

	CHECK(Graphite::FailureMessage(NetworkOperation::Fetch, failure, NetworkNames{.Remote = "upstream"}) ==
		  "Git could not sign in to upstream. Run `git fetch` once in a terminal to enter credentials or accept the host key, or "
		  "add your key to ssh-agent.\ngit@example.com: Permission denied (publickey).");

	CHECK(Graphite::FailureMessage(NetworkOperation::Fetch, failure, NetworkNames{}) == failure.Shown);
}

TEST_CASE("Network text: a failure without a hint is git's words as they are")
{
	const Core::NetworkFailure failure{.Shown = "fatal: 'nowhere' does not appear to be a git repository",
									   .Copied = "fatal: 'nowhere' does not appear to be a git repository\n"};

	CHECK(Graphite::FailureMessage(NetworkOperation::Fetch, failure, NetworkNames{.Remote = "origin"}) == failure.Shown);
}

TEST_CASE("Network text: pull's local-changes failure lists the files git named across the whole of its errors")
{
	const std::string errors = "error: Your local changes to the following files would be overwritten by merge:\n"
							   "\ta.txt\n"
							   "Please commit your changes or stash them before you merge.\n"
							   "error: The following untracked working tree files would be overwritten by merge:\n"
							   "\tsrc/new file.txt\r\n"
							   "Please move or remove them before you merge.\n"
							   "Aborting\n";
	const Core::NetworkFailure failure{.Shown = "Please move or remove them before you merge.\nAborting", .Copied = errors};

	CHECK(Graphite::FailureMessage(NetworkOperation::Pull, failure, NetworkNames{}) ==
		  "Commit or stash your changes to these files first:\na.txt\nsrc/new file.txt");
}

TEST_CASE("Network text: pull's local-changes failure lists ten files and counts the rest")
{
	const Core::NetworkFailure failure{.Shown = "Aborting", .Copied = LocalChangesStderr(13)};

	CHECK(Graphite::FailureMessage(NetworkOperation::Pull, failure, NetworkNames{}) ==
		  "Commit or stash your changes to these files first:\nfile1.txt\nfile2.txt\nfile3.txt\nfile4.txt\nfile5.txt\nfile6.txt\n"
		  "file7.txt\nfile8.txt\nfile9.txt\nfile10.txt\nand 3 more");

	const Core::NetworkFailure tenFiles{.Shown = "Aborting", .Copied = LocalChangesStderr(10)};
	const std::string tenListed = Graphite::FailureMessage(NetworkOperation::Pull, tenFiles, NetworkNames{});

	CHECK(tenListed.ends_with("\nfile10.txt"));
}

TEST_CASE("Network text: only pull reads a list of local changes, which only a merge prints")
{
	const Core::NetworkFailure failure{.Shown = "Aborting", .Copied = LocalChangesStderr(2)};

	CHECK(Graphite::FailureMessage(NetworkOperation::Fetch, failure, NetworkNames{}) == "Aborting");
}

TEST_CASE("Network text: the activity line reads the running operation, else the done text while it is shown")
{
	const Graphite::NetworkActivity running =
		Graphite::DescribeNetworkActivity(NetworkState{.Running = NetworkOperation::Fetch, .Percent = 45}, std::nullopt);

	CHECK(running.Running == std::optional<std::string>{"Fetching" + kEllipsis + " 45%"});
	CHECK_FALSE(running.Done.has_value());

	const Graphite::NetworkActivity done = Graphite::DescribeNetworkActivity(
		NetworkState{}, NetworkDone{.Operation = NetworkOperation::Push, .Commits = 2});

	CHECK_FALSE(done.Running.has_value());
	CHECK(done.Done == std::optional<std::string>{"Pushed 2 commits"});

	const Graphite::NetworkActivity idle = Graphite::DescribeNetworkActivity(NetworkState{}, std::nullopt);

	CHECK_FALSE(idle.Running.has_value());
	CHECK_FALSE(idle.Done.has_value());
}

TEST_CASE("Network text: a pull or a push of no commits says there was nothing to do")
{
	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::Pull, .Commits = 0}) == "Already up to date");
	CHECK(Graphite::DoneText(NetworkDone{.Operation = NetworkOperation::Push, .Commits = 0}) == "Nothing to push");
}
