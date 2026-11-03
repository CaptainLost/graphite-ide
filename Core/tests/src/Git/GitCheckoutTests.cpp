#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

	bool ListsPath(const std::vector<std::filesystem::path>& paths, const char* path)
	{
		return std::find(paths.begin(), paths.end(), std::filesystem::path{path}) != paths.end();
	}

	const Core::Branch* FindLocalBranch(const std::vector<Core::Branch>& branches, const std::string& name)
	{
		for (const Core::Branch& branch : branches)
		{
			if (branch.Kind == Core::RefKind::Local && branch.Name == name)
			{
				return &branch;
			}
		}

		return nullptr;
	}

	bool HasLocalBranch(const Core::Repository& repository, const std::string& name)
	{
		const Core::Result<std::vector<Core::Branch>> branches = repository.QueryBranches();
		REQUIRE(branches.HasValue());

		return FindLocalBranch(branches.GetValue(), name) != nullptr;
	}

	Core::CheckoutTarget LocalBranch(const std::string& name)
	{
		return {.Kind = Core::CheckoutKind::LocalBranch, .Name = name};
	}

	void MakeFeatureBranchDifferingOnlyInA(GitFixture& fixture)
	{
		fixture.Write("a.txt", "one\n");
		fixture.Write("b.txt", "same\n");
		fixture.Commit("base");
		fixture.Branch("feature");
		fixture.Checkout("feature");
		fixture.Write("a.txt", "feature\n");
		fixture.Commit("feature change");
		fixture.Checkout("master");
	}

	void MakeFeatureBranchAddingOneFileAndRemovingAnother(GitFixture& fixture)
	{
		fixture.Write("a.txt", "one\n");
		fixture.Write("gone.txt", "only on master\n");
		fixture.Commit("base");
		fixture.Branch("feature");
		fixture.Checkout("feature");
		fixture.Remove("gone.txt");
		fixture.Write("added.txt", "only on feature\n");
		fixture.Commit("add one, remove one");
		fixture.Checkout("master");
	}

	void WriteOperationMarkers(GitFixture& fixture, const std::string& headCommit)
	{
		for (const char* markerName : {"MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD", "AUTO_MERGE"})
		{
			fixture.Write(std::string(".git/") + markerName, headCommit + "\n");
		}

		for (const char* markerName : {"MERGE_MSG", "MERGE_MODE", "MERGE_RR", "SQUASH_MSG", "BISECT_LOG"})
		{
			fixture.Write(std::string(".git/") + markerName, "\n");
		}

		fixture.Write(".git/rebase-merge/head-name", "refs/heads/master\n");
		fixture.Write(".git/rebase-apply/next", "1\n");
		fixture.Write(".git/sequencer/todo", "pick " + headCommit + "\n");
	}

	void CheckMergeCherryPickAndRevertEndedButRebaseSequenceAndBisectKept(const GitFixture& fixture)
	{
		const std::filesystem::path gitDirectory = fixture.GetPath() / ".git";

		for (const char* markerName : {"MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD", "AUTO_MERGE", "MERGE_MSG",
									   "MERGE_MODE", "MERGE_RR", "SQUASH_MSG"})
		{
			CAPTURE(markerName);
			CHECK_FALSE(std::filesystem::exists(gitDirectory / markerName));
		}

		CHECK(std::filesystem::exists(gitDirectory / "BISECT_LOG"));
		CHECK(std::filesystem::exists(gitDirectory / "rebase-merge" / "head-name"));
		CHECK(std::filesystem::exists(gitDirectory / "rebase-apply" / "next"));
		CHECK(std::filesystem::exists(gitDirectory / "sequencer" / "todo"));
	}
}

TEST_CASE("A clean tree plans no overwrite and lists the files the checkout would change")
{
	GitFixture fixture;
	MakeFeatureBranchDifferingOnlyInA(fixture);

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::CheckoutPlan> plan = repository.PlanCheckout(LocalBranch("feature"));

	REQUIRE(plan.HasValue());
	CHECK(plan.GetValue().Overwritten.empty());
	CHECK(ListsPath(plan.GetValue().Updated, "a.txt"));
	CHECK_FALSE(ListsPath(plan.GetValue().Updated, "b.txt"));
}

TEST_CASE("A plan lists the files the checkout would create and the ones it would remove")
{
	GitFixture fixture;
	MakeFeatureBranchAddingOneFileAndRemovingAnother(fixture);

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::CheckoutPlan> plan = repository.PlanCheckout(LocalBranch("feature"));

	REQUIRE(plan.HasValue());
	CHECK(plan.GetValue().Overwritten.empty());
	CHECK(ListsPath(plan.GetValue().Updated, "added.txt"));
	CHECK(ListsPath(plan.GetValue().Updated, "gone.txt"));
	CHECK_FALSE(ListsPath(plan.GetValue().Updated, "a.txt"));
	CHECK(std::filesystem::exists(fixture.GetPath() / "gone.txt"));
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "added.txt"));
}

TEST_CASE("A change to a file the branches differ in is planned as overwritten, and planning touches nothing")
{
	GitFixture fixture;
	MakeFeatureBranchDifferingOnlyInA(fixture);
	fixture.Write("a.txt", "my edit\n");
	fixture.Write("b.txt", "my other edit\n");

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::CheckoutPlan> plan = repository.PlanCheckout(LocalBranch("feature"));

	REQUIRE(plan.HasValue());
	CHECK(ListsPath(plan.GetValue().Overwritten, "a.txt"));
	CHECK_FALSE(ListsPath(plan.GetValue().Overwritten, "b.txt"));
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "my edit\n");
	CHECK(repository.QueryHead().GetValue().BranchName == "master");
}

TEST_CASE("A change to a file the target removes is planned as overwritten, not as removed")
{
	GitFixture fixture;
	MakeFeatureBranchAddingOneFileAndRemovingAnother(fixture);
	fixture.Write("gone.txt", "my edit\n");

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::CheckoutPlan> plan = repository.PlanCheckout(LocalBranch("feature"));

	REQUIRE(plan.HasValue());
	CHECK(ListsPath(plan.GetValue().Overwritten, "gone.txt"));
	CHECK_FALSE(ListsPath(plan.GetValue().Updated, "gone.txt"));
}

TEST_CASE("Checkout switches branch, carries changes the branches do not differ in, and reports what it changed")
{
	GitFixture fixture;
	MakeFeatureBranchDifferingOnlyInA(fixture);
	fixture.Write("b.txt", "carried\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::CheckoutOutcome changed = repository.Checkout(LocalBranch("feature"));

	REQUIRE_FALSE(changed.Failure.has_value());
	CHECK(ListsPath(changed.Changed, "a.txt"));
	CHECK(repository.QueryHead().GetValue().BranchName == "feature");
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "feature\n");
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "b.txt") == "carried\n");
}

TEST_CASE("Checkout reports the files it created and the ones it removed")
{
	GitFixture fixture;
	MakeFeatureBranchAddingOneFileAndRemovingAnother(fixture);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::CheckoutOutcome changed = repository.Checkout(LocalBranch("feature"));

	REQUIRE_FALSE(changed.Failure.has_value());
	CHECK(ListsPath(changed.Changed, "added.txt"));
	CHECK(ListsPath(changed.Changed, "gone.txt"));
	CHECK_FALSE(ListsPath(changed.Changed, "a.txt"));
	CHECK(std::filesystem::exists(fixture.GetPath() / "added.txt"));
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "gone.txt"));
}

TEST_CASE("Checkout refuses to overwrite a change and leaves everything as it was")
{
	GitFixture fixture;
	MakeFeatureBranchDifferingOnlyInA(fixture);
	fixture.Write("a.txt", "my edit\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(repository.Checkout(LocalBranch("feature")).Failure.has_value());
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "my edit\n");
	CHECK(repository.QueryHead().GetValue().BranchName == "master");
}

TEST_CASE("A refused checkout reports the failure and names no changed path")
{
	GitFixture fixture;
	MakeFeatureBranchAddingOneFileAndRemovingAnother(fixture);
	fixture.Write("gone.txt", "my edit\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::CheckoutOutcome outcome = repository.Checkout(LocalBranch("feature"));

	REQUIRE(outcome.Failure.has_value());
	CHECK(outcome.Failure->Code == Core::ErrorCode::InvalidArgument);
	CHECK(outcome.Changed.empty());
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "added.txt"));
}

TEST_CASE("Checkout refused over one file writes and removes none of the others")
{
	GitFixture fixture;
	MakeFeatureBranchAddingOneFileAndRemovingAnother(fixture);
	fixture.Write("added.txt", "my untracked work\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(repository.Checkout(LocalBranch("feature")).Failure.has_value());
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "added.txt") == "my untracked work\n");
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "gone.txt") == "only on master\n");
	CHECK(repository.QueryHead().GetValue().BranchName == "master");
}

TEST_CASE("Checking out a commit detaches HEAD there")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("one");
	fixture.Write("a.txt", "two\n");
	fixture.Commit("two");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::LogRequest wholeHistory{};
	const Core::Oid target = repository.QueryLog(wholeHistory).GetValue().Commits.back().Id;

	REQUIRE_FALSE(repository.Checkout({.Kind = Core::CheckoutKind::Commit, .Commit = target}).Failure.has_value());

	const Core::HeadState head = repository.QueryHead().GetValue();
	CHECK(head.Kind == Core::HeadKind::Detached);
	CHECK(head.Commit->ToHex() == first);
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "one\n");
}

TEST_CASE("Checking out a remote branch creates a local branch tracking it")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Write("a.txt", "remote\n");
	const std::string remoteTip = fixture.Commit("remote work");
	fixture.PushTo("topic", remoteTip);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE_FALSE(repository.Checkout({.Kind = Core::CheckoutKind::RemoteBranch, .Name = "origin/topic"}).Failure.has_value());

	const Core::HeadState head = repository.QueryHead().GetValue();
	CHECK(head.BranchName == "topic");

	const std::vector<Core::Branch> branches = repository.QueryBranches().GetValue();
	const Core::Branch* topic = FindLocalBranch(branches, "topic");
	REQUIRE(topic != nullptr);
	REQUIRE(topic->Upstream.has_value());
	CHECK(*topic->Upstream == "origin/topic");
}

TEST_CASE("A refused checkout of a remote branch leaves no local branch behind")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string remoteTip = fixture.Commit("one");
	fixture.Write("a.txt", "two\n");
	fixture.Commit("two");
	fixture.PushTo("topic", remoteTip);
	fixture.Write("a.txt", "my edit\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(repository.Checkout({.Kind = Core::CheckoutKind::RemoteBranch, .Name = "origin/topic"}).Failure.has_value());
	CHECK_FALSE(HasLocalBranch(repository, "topic"));
	CHECK(repository.QueryHead().GetValue().BranchName == "master");
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "my edit\n");
}

TEST_CASE("Checking out a remote branch whose local namesake exists switches to it without moving it")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string base = fixture.Commit("one");
	fixture.Branch("topic");
	fixture.Write("a.txt", "remote\n");
	const std::string remoteTip = fixture.Commit("remote work");
	fixture.PushTo("topic", remoteTip);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE_FALSE(repository.Checkout({.Kind = Core::CheckoutKind::RemoteBranch, .Name = "origin/topic"}).Failure.has_value());

	const Core::HeadState head = repository.QueryHead().GetValue();
	CHECK(head.BranchName == "topic");
	CHECK(head.Commit->ToHex() == base);
}

TEST_CASE("Checking out a branch that does not exist is NotFound")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::CheckoutOutcome changed = repository.Checkout(LocalBranch("nowhere"));
	REQUIRE(changed.Failure.has_value());
	CHECK(changed.Failure->Code == Core::ErrorCode::NotFound);
	CHECK(changed.Changed.empty());
}

TEST_CASE("Discarding everything resets tracked changes and removes untracked files only when asked")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Write("a.txt", "edit\n");
	fixture.Write("new.txt", "fresh\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.DiscardAll(Core::UntrackedFiles::Excluded).HasValue());
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "one\n");
	CHECK(std::filesystem::exists(fixture.GetPath() / "new.txt"));

	REQUIRE(repository.DiscardAll(Core::UntrackedFiles::Included).HasValue());
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "new.txt"));
}

TEST_CASE("Discarding everything ends a merge, a cherry-pick or a revert, as git does, but not a rebase or a bisect")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string head = fixture.Commit("one");
	WriteOperationMarkers(fixture, head);
	fixture.Write("a.txt", "edit\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.DiscardAll(Core::UntrackedFiles::Excluded).HasValue());
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "one\n");
	CheckMergeCherryPickAndRevertEndedButRebaseSequenceAndBisectKept(fixture);
}

TEST_CASE("Discarding everything drops what is staged too, a staged new file included")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Write("a.txt", "staged edit\n");
	fixture.Write("added.txt", "staged new\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	REQUIRE(repository.Stage({"a.txt", "added.txt"}).HasValue());

	REQUIRE(repository.DiscardAll(Core::UntrackedFiles::Excluded).HasValue());
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "one\n");
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "added.txt"));

	const Core::Result<Core::WorkingTreeStatus> status = repository.QueryStatus();
	REQUIRE(status.HasValue());
	CHECK(status.GetValue().Entries.empty());
}

TEST_CASE("Discarding untracked files leaves ignored ones, even inside an untracked directory")
{
	GitFixture fixture;
	fixture.Write(".gitignore", "*.log\n");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Write("top.log", "ignored\n");
	fixture.Write("scratch/note.txt", "untracked\n");
	fixture.Write("scratch/build.log", "ignored inside an untracked directory\n");
	fixture.Write("drafts/deep/only.txt", "untracked\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.DiscardAll(Core::UntrackedFiles::Included).HasValue());
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "scratch" / "note.txt"));
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "drafts"));
	CHECK(std::filesystem::exists(fixture.GetPath() / "top.log"));
	CHECK(std::filesystem::exists(fixture.GetPath() / "scratch" / "build.log"));
}

TEST_CASE("Discarding untracked files leaves an untracked nested repository alone")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Write("sub/file", "inside a repository of its own\n");

	const unsigned int isBare = 0;
	git_repository* nested = nullptr;
	REQUIRE(git_repository_init(&nested, Core::ToUtf8(fixture.GetPath() / "sub").c_str(), isBare) == 0);
	git_repository_free(nested);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.DiscardAll(Core::UntrackedFiles::Included).HasValue());
	CHECK(std::filesystem::exists(fixture.GetPath() / "sub" / ".git"));
	CHECK(std::filesystem::exists(fixture.GetPath() / "sub" / "file"));
}

TEST_CASE("With no commit yet, planning, checking out and discarding fail as NotFound and touch nothing")
{
	GitFixture fixture;
	fixture.Write("a.txt", "not committed\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<Core::CheckoutPlan> plan = repository.PlanCheckout(LocalBranch("master"));
	REQUIRE_FALSE(plan.HasValue());
	CHECK(plan.GetError().Code == Core::ErrorCode::NotFound);

	const Core::CheckoutOutcome outcome = repository.Checkout(LocalBranch("master"));
	REQUIRE(outcome.Failure.has_value());
	CHECK(outcome.Failure->Code == Core::ErrorCode::NotFound);
	CHECK(outcome.Changed.empty());

	const Core::Result<void> discarded = repository.DiscardAll(Core::UntrackedFiles::Included);
	REQUIRE_FALSE(discarded.HasValue());
	CHECK(discarded.GetError().Code == Core::ErrorCode::NotFound);

	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "not committed\n");
}

TEST_CASE("From an unborn branch, a checkout writes the target's files and removes nothing")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	const char* orphanBranchWithNoCommit = "refs/heads/orphan";
	git_repository* headMovingRepository = nullptr;
	REQUIRE(git_repository_open(&headMovingRepository, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);
	REQUIRE(git_repository_set_head(headMovingRepository, orphanBranchWithNoCommit) == 0);
	git_repository_free(headMovingRepository);
	fixture.Remove("a.txt");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<Core::CheckoutPlan> plan = repository.PlanCheckout(LocalBranch("master"));
	REQUIRE(plan.HasValue());
	CHECK(plan.GetValue().Overwritten.empty());
	CHECK(ListsPath(plan.GetValue().Updated, "a.txt"));

	const Core::CheckoutOutcome outcome = repository.Checkout(LocalBranch("master"));
	REQUIRE_FALSE(outcome.Failure.has_value());
	CHECK(outcome.Changed == std::vector<std::filesystem::path>{"a.txt"});
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "one\n");
	CHECK(repository.QueryHead().GetValue().BranchName == "master");
}

TEST_CASE("An untracked file the target tracks is planned as overwritten")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("feature");
	fixture.Checkout("feature");
	fixture.Write("notes.txt", "tracked on feature\n");
	fixture.Commit("add notes");
	fixture.Checkout("master");
	fixture.Write("notes.txt", "my untracked notes\n");

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::CheckoutPlan> plan = repository.PlanCheckout(LocalBranch("feature"));

	REQUIRE(plan.HasValue());
	CHECK(ListsPath(plan.GetValue().Overwritten, "notes.txt"));
}

namespace {

	void StopMasterMidMergeOfSideOnAConflictInAAndBranchElsewhereAddingC(GitFixture& fixture)
	{
		fixture.Write("a.txt", "base\n");
		fixture.Write("b.txt", "same\n");
		fixture.Commit("base");
		fixture.Branch("side");
		fixture.Branch("elsewhere");
		fixture.Write("a.txt", "ours\n");
		fixture.Commit("ours");
		fixture.Checkout("side");
		fixture.Write("a.txt", "theirs\n");
		fixture.Commit("theirs");
		fixture.Checkout("elsewhere");
		fixture.Write("c.txt", "only elsewhere\n");
		fixture.Commit("elsewhere");
		fixture.Checkout("master");
		fixture.StartConflict("side");
	}
}

TEST_CASE("Unresolved conflicts in the index are planned as overwritten, beside what the checkout would write")
{
	GitFixture fixture;
	StopMasterMidMergeOfSideOnAConflictInAAndBranchElsewhereAddingC(fixture);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::CheckoutPlan> plan = repository.PlanCheckout(LocalBranch("elsewhere"));

	REQUIRE(plan.HasValue());
	CHECK(ListsPath(plan.GetValue().Overwritten, "a.txt"));
	CHECK(ListsPath(plan.GetValue().Updated, "c.txt"));
	CHECK_FALSE(ListsPath(plan.GetValue().Updated, "a.txt"));
}

TEST_CASE("Checkout over unresolved conflicts is refused as such, and goes ahead once they are discarded")
{
	GitFixture fixture;
	StopMasterMidMergeOfSideOnAConflictInAAndBranchElsewhereAddingC(fixture);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::CheckoutOutcome refused = repository.Checkout(LocalBranch("elsewhere"));

	REQUIRE(refused.Failure.has_value());
	CHECK(refused.Failure->Code == Core::ErrorCode::InvalidArgument);
	CHECK(refused.Failure->Message.find("resolve or discard the conflicts first") != std::string::npos);
	CHECK(refused.Changed.empty());
	CHECK(repository.QueryHead().GetValue().BranchName == "master");

	REQUIRE(repository.DiscardAll(Core::UntrackedFiles::Excluded).HasValue());

	const Core::CheckoutOutcome outcome = repository.Checkout(LocalBranch("elsewhere"));
	CHECK_FALSE(outcome.Failure.has_value());
	CHECK(repository.QueryHead().GetValue().BranchName == "elsewhere");
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "c.txt") == "only elsewhere\n");
}
