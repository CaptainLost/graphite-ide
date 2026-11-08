#include <doctest/doctest.h>

#include "Git/GitActivity.h"

#include <optional>
#include <string>

using Graphite::GitModel;

namespace {

	const std::string kEllipsis = "\xE2\x80\xA6";

	GitModel::WriteSubject PathsSubject(GitModel::WriteKind kind, std::size_t pathCount, std::string onePathName = {})
	{
		return GitModel::WriteSubject{.Kind = kind, .PathCount = pathCount, .OnePathName = std::move(onePathName)};
	}

	GitModel::WriteSubject NamedSubject(GitModel::WriteKind kind, std::string name, std::string newName = {})
	{
		return GitModel::WriteSubject{.Kind = kind, .Name = std::move(name), .NewName = std::move(newName)};
	}

	GitModel::WriteSubject StashSubject(GitModel::WriteKind kind, std::optional<std::size_t> index)
	{
		return GitModel::WriteSubject{.Kind = kind, .StashIndex = index};
	}

	Core::CheckoutTarget LocalTarget(const char* name)
	{
		return Core::CheckoutTarget{.Kind = Core::CheckoutKind::LocalBranch, .Name = name};
	}
}

TEST_CASE("A write of many files counts them in groups of three digits, while it runs and once it is done")
{
	const GitModel::WriteSubject staging = PathsSubject(GitModel::WriteKind::Stage, 2000);

	CHECK(Graphite::WriteRunningText(staging) == "Staging 2,000 files" + kEllipsis);
	CHECK(Graphite::WriteDoneText(staging) == "Staged 2,000 files");
}

TEST_CASE("A write of one path names it")
{
	const GitModel::WriteSubject unstaging = PathsSubject(GitModel::WriteKind::Unstage, 1, "Main.cpp");
	const GitModel::WriteSubject discarding = PathsSubject(GitModel::WriteKind::Discard, 1, "Main.cpp");

	CHECK(Graphite::WriteRunningText(unstaging) == "Unstaging Main.cpp" + kEllipsis);
	CHECK(Graphite::WriteDoneText(unstaging) == "Unstaged Main.cpp");
	CHECK(Graphite::WriteRunningText(discarding) == "Discarding changes in Main.cpp" + kEllipsis);
	CHECK(Graphite::WriteDoneText(discarding) == "Discarded changes in Main.cpp");
}

TEST_CASE("A discard of several files counts them")
{
	CHECK(Graphite::WriteRunningText(PathsSubject(GitModel::WriteKind::Discard, 3)) ==
		  "Discarding changes in 3 files" + kEllipsis);
}

TEST_CASE("Branch writes name their branches")
{
	CHECK(Graphite::WriteRunningText(NamedSubject(GitModel::WriteKind::CreateBranch, "x")) == "Creating branch x" + kEllipsis);
	CHECK(Graphite::WriteDoneText(NamedSubject(GitModel::WriteKind::CreateBranch, "x")) == "Created branch x");
	CHECK(Graphite::WriteRunningText(NamedSubject(GitModel::WriteKind::RenameBranch, "x", "y")) ==
		  "Renaming branch x to y" + kEllipsis);
	CHECK(Graphite::WriteDoneText(NamedSubject(GitModel::WriteKind::RenameBranch, "x", "y")) == "Renamed branch x to y");
	CHECK(Graphite::WriteRunningText(NamedSubject(GitModel::WriteKind::DeleteBranch, "x")) == "Deleting branch x" + kEllipsis);
	CHECK(Graphite::WriteDoneText(NamedSubject(GitModel::WriteKind::DeleteBranch, "x")) == "Deleted branch x");
	CHECK(Graphite::WriteRunningText(NamedSubject(GitModel::WriteKind::Checkout, "feature/x")) ==
		  "Checking out feature/x" + kEllipsis);
	CHECK(Graphite::WriteDoneText(NamedSubject(GitModel::WriteKind::Checkout, "feature/x")) == "Checked out feature/x");
}

TEST_CASE("Stash writes name the stash by its place in the list, or as the stash when the list was not known")
{
	CHECK(Graphite::WriteRunningText(StashSubject(GitModel::WriteKind::StashApply, 0)) == "Applying stash@{0}" + kEllipsis);
	CHECK(Graphite::WriteDoneText(StashSubject(GitModel::WriteKind::StashApply, 0)) == "Applied stash@{0}");
	CHECK(Graphite::WriteRunningText(StashSubject(GitModel::WriteKind::StashPop, 2)) == "Popping stash@{2}" + kEllipsis);
	CHECK(Graphite::WriteDoneText(StashSubject(GitModel::WriteKind::StashPop, 2)) == "Popped stash@{2}");
	CHECK(Graphite::WriteRunningText(StashSubject(GitModel::WriteKind::StashDrop, std::nullopt)) ==
		  "Dropping the stash" + kEllipsis);
	CHECK(Graphite::WriteRunningText(StashSubject(GitModel::WriteKind::StashSave, std::nullopt)) == "Stashing" + kEllipsis);
}

TEST_CASE("A commit says it is committing, then names its hash and the first line of its message")
{
	Core::Oid committed;
	committed.Bytes[0] = 0xa1;
	committed.Bytes[1] = 0xb2;

	GitModel::WriteSubject commit{.Kind = GitModel::WriteKind::Commit, .CommitSummary = "Fix the parser"};
	CHECK(Graphite::WriteRunningText(commit) == "Committing" + kEllipsis);

	commit.Committed = committed;
	CHECK(Graphite::WriteDoneText(commit) == "Committed " + committed.ShortHex() + " Fix the parser");

	commit.CommitSummary.clear();
	CHECK(Graphite::WriteDoneText(commit) == "Committed " + committed.ShortHex());
}

TEST_CASE("An init says it initialises the repository")
{
	const GitModel::WriteSubject init{.Kind = GitModel::WriteKind::Init};

	CHECK(Graphite::WriteRunningText(init) == "Initialising the repository" + kEllipsis);
	CHECK(Graphite::WriteDoneText(init) == "Initialised the repository");
}

TEST_CASE("A running write is drawn after a loader, and one done is drawn plain")
{
	const GitModel::WriteSubject staging = PathsSubject(GitModel::WriteKind::Stage, 2);

	const std::optional<Graphite::GitActivity> running = Graphite::DescribeGitActivity(
		GitModel::WriteReport{.Subject = staging, .Progress = GitModel::WriteProgress::Running}, std::nullopt);
	const std::optional<Graphite::GitActivity> done = Graphite::DescribeGitActivity(
		GitModel::WriteReport{.Subject = staging, .Progress = GitModel::WriteProgress::Done}, std::nullopt);

	REQUIRE(running.has_value());
	CHECK(running->Text == "Staging 2 files" + kEllipsis);
	CHECK(running->Busy);
	CHECK_FALSE(running->OpensGit);
	REQUIRE(done.has_value());
	CHECK(done->Text == "Staged 2 files");
	CHECK_FALSE(done->Busy);
}

TEST_CASE("A running checkout is left to the branch item, which names it in place of the head, but its end is confirmed")
{
	const GitModel::WriteSubject checkout = NamedSubject(GitModel::WriteKind::Checkout, "feature/x");

	CHECK_FALSE(Graphite::DescribeGitActivity(
					GitModel::WriteReport{.Subject = checkout, .Progress = GitModel::WriteProgress::Running}, std::nullopt)
					.has_value());

	const std::optional<Graphite::GitActivity> done = Graphite::DescribeGitActivity(
		GitModel::WriteReport{.Subject = checkout, .Progress = GitModel::WriteProgress::Done}, std::nullopt);

	REQUIRE(done.has_value());
	CHECK(done->Text == "Checked out feature/x");
}

TEST_CASE("A question waiting in Git is said first, and a click on it brings the Git panel forward")
{
	const GitModel::WriteSubject staging = PathsSubject(GitModel::WriteKind::Stage, 2);

	const std::optional<Graphite::GitActivity> activity = Graphite::DescribeGitActivity(
		GitModel::WriteReport{.Subject = staging, .Progress = GitModel::WriteProgress::Running},
		std::string{"Checkout of feature/x"});

	REQUIRE(activity.has_value());
	CHECK(activity->Text == "Checkout of feature/x is waiting for your answer in Git");
	CHECK(activity->OpensGit);
	CHECK_FALSE(activity->Busy);
}

TEST_CASE("Nothing is said with no write and no question")
{
	CHECK_FALSE(Graphite::DescribeGitActivity(std::nullopt, std::nullopt).has_value());
}

TEST_CASE("A checkout is named in place of the head from its plan, through its question, to the model's write")
{
	const Graphite::GitPanelCheckouts planning{.Planning = LocalTarget("a")};
	const Graphite::GitPanelCheckouts asking{.Asking = LocalTarget("b")};
	const Graphite::GitPanelCheckouts writing{.Writing = LocalTarget("c")};

	CHECK(Graphite::DescribePanelWait(planning).CheckingOut == "a");
	CHECK(Graphite::DescribePanelWait(asking).CheckingOut == "b");
	CHECK(Graphite::DescribePanelWait(writing).CheckingOut == "c");
	CHECK_FALSE(Graphite::DescribePanelWait({}).CheckingOut.has_value());
}

TEST_CASE("A checkout whose question waits for the Git panel says so instead of naming it in place of the head")
{
	const Graphite::GitPanelWait wait = Graphite::DescribePanelWait({.HeldForPanel = LocalTarget("feature/x")});

	CHECK(wait.AwaitingAnswer == "Checkout of feature/x");
	CHECK_FALSE(wait.CheckingOut.has_value());
}

TEST_CASE("A delete whose question waits for the Git panel says so")
{
	const Graphite::GitPanelWait wait = Graphite::DescribePanelWait({.DeleteHeldForPanel = "old"});

	CHECK(wait.AwaitingAnswer == "Deleting branch old");
}

TEST_CASE("Pull's fast-forward reads as pulling while it runs and counts the commits it brought once done")
{
	const GitModel::WriteSubject pulled{.Kind = GitModel::WriteKind::FastForward, .Commits = 3};

	CHECK(Graphite::WriteRunningText(pulled) == "Pulling" + kEllipsis);
	CHECK(Graphite::WriteDoneText(pulled) == "Pulled 3 commits");
}

TEST_CASE("A running network operation is said over a write's done text and gives way to a running write")
{
	const GitModel::WriteSubject staging = PathsSubject(GitModel::WriteKind::Stage, 1, "a.txt");
	const GitModel::WriteReport stageRunning{.Subject = staging, .Progress = GitModel::WriteProgress::Running};
	const GitModel::WriteReport stageDone{.Subject = staging, .Progress = GitModel::WriteProgress::Done};
	const Graphite::NetworkActivity fetching{.Running = "Fetching" + kEllipsis + " 45%"};

	const std::optional<Graphite::GitActivity> overDone = Graphite::DescribeGitActivity(stageDone, std::nullopt, fetching);
	REQUIRE(overDone.has_value());
	CHECK(overDone->Text == "Fetching" + kEllipsis + " 45%");
	CHECK(overDone->Busy);

	const std::optional<Graphite::GitActivity> givesWay = Graphite::DescribeGitActivity(stageRunning, std::nullopt, fetching);
	REQUIRE(givesWay.has_value());
	CHECK(givesWay->Text == "Staging a.txt" + kEllipsis);
}

TEST_CASE("A checkout named in place of the head leaves the activity line to a running network operation")
{
	const GitModel::WriteReport checkingOut{.Subject = NamedSubject(GitModel::WriteKind::Checkout, "feature"),
											.Progress = GitModel::WriteProgress::Running};

	const std::optional<Graphite::GitActivity> activity =
		Graphite::DescribeGitActivity(checkingOut, std::nullopt, Graphite::NetworkActivity{.Running = "Pushing" + kEllipsis});

	REQUIRE(activity.has_value());
	CHECK(activity->Text == "Pushing" + kEllipsis);
}

TEST_CASE("A network operation's done text is said once nothing runs and no write's done text is shown")
{
	const std::optional<Graphite::GitActivity> activity =
		Graphite::DescribeGitActivity(std::nullopt, std::nullopt, Graphite::NetworkActivity{.Done = "Fetched"});

	REQUIRE(activity.has_value());
	CHECK(activity->Text == "Fetched");
	CHECK_FALSE(activity->Busy);
}
