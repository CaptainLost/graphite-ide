#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <algorithm>
#include <compare>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using Core::Result;
using Core::UntrackedFiles;

namespace {

	struct ChangedPath
	{
		std::string Path;
		Core::FileChange Change;

		friend std::strong_ordering operator<=>(const ChangedPath&, const ChangedPath&) = default;
	};

	void CommitOneFileAsTester(GitFixture& fixture)
	{
		fixture.SetIdentity("Tester", "tester@example.com");
		fixture.Write("a.txt", "one\n");
		fixture.Commit("one");
	}

	std::vector<ChangedPath> SortedChangedPathsOfStash(const Core::Repository& repository,
													   const Core::Oid& stash,
													   Core::DiffTarget target)
	{
		const Result<Core::Diff> diff = repository.QueryDiff(Core::DiffRequest{.Target = target, .From = stash});
		REQUIRE(diff.HasValue());

		std::vector<ChangedPath> found;

		for (const Core::FileDiff& file : diff.GetValue().Files)
		{
			found.push_back(ChangedPath{.Path = Core::ToGenericUtf8(file.NewPath), .Change = file.Change});
		}

		std::ranges::sort(found);
		return found;
	}

	// libgit2 caches an index per handle, and the application keeps one handle per worker thread.
	void FillIndexCache(const Core::Repository& handle)
	{
		REQUIRE(handle.QueryStatus().HasValue());
	}
}

TEST_CASE("Stashing takes tracked changes off the tree and lists them")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);
	fixture.Write("a.txt", "changed\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<bool> saved = repository.StashSave("work in progress", UntrackedFiles::Excluded);
	REQUIRE(saved.HasValue());
	CHECK(saved.GetValue());

	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "one\n");

	const Result<std::vector<Core::StashEntry>> stashes = repository.QueryStashes();
	REQUIRE(stashes.HasValue());
	REQUIRE(stashes.GetValue().size() == 1);
	CHECK(stashes.GetValue()[0].Index == 0);
	CHECK(stashes.GetValue()[0].Message.find("work in progress") != std::string::npos);
	CHECK(stashes.GetValue()[0].SecondsSinceEpoch > 0);
}

TEST_CASE("Untracked files are stashed only when asked and stashing no tracked change is no error")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);
	fixture.Write("new.txt", "fresh\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<bool> stashWithNoTrackedChange = repository.StashSave("nothing", UntrackedFiles::Excluded);
	REQUIRE(stashWithNoTrackedChange.HasValue());
	CHECK_FALSE(stashWithNoTrackedChange.GetValue());
	CHECK(std::filesystem::exists(fixture.GetPath() / "new.txt"));

	const Result<bool> stashWithUntracked = repository.StashSave("with untracked", UntrackedFiles::Included);
	REQUIRE(stashWithUntracked.HasValue());
	CHECK(stashWithUntracked.GetValue());
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "new.txt"));
}

TEST_CASE("Apply restores a stash and keeps it, pop restores and removes it")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);
	fixture.Write("a.txt", "changed\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	REQUIRE(repository.StashSave("one", UntrackedFiles::Excluded).HasValue());
	const Core::Oid stashId = repository.QueryStashes().GetValue()[0].Id;

	REQUIRE(repository.StashApply(stashId).HasValue());
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "changed\n");
	CHECK(repository.QueryStashes().GetValue().size() == 1);

	fixture.Write("a.txt", "one\n");

	REQUIRE(repository.StashPop(stashId).HasValue());
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "changed\n");
	CHECK(repository.QueryStashes().GetValue().empty());
}

TEST_CASE("A stash is addressed by id, so a stash pushed meanwhile does not shift the target")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	fixture.Write("a.txt", "first\n");
	REQUIRE(repository.StashSave("first", UntrackedFiles::Excluded).HasValue());
	const Core::Oid firstStash = repository.QueryStashes().GetValue()[0].Id;

	fixture.Write("a.txt", "second\n");
	REQUIRE(repository.StashSave("second", UntrackedFiles::Excluded).HasValue());

	REQUIRE(repository.StashDrop(firstStash).HasValue());

	const std::vector<Core::StashEntry> remaining = repository.QueryStashes().GetValue();
	REQUIRE(remaining.size() == 1);
	CHECK(remaining[0].Message.find("second") != std::string::npos);

	CHECK(repository.StashDrop(firstStash).GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("Popping onto a conflicting change fails and keeps the stash")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	fixture.Write("a.txt", "stashed\n");
	REQUIRE(repository.StashSave("s", UntrackedFiles::Excluded).HasValue());
	const Core::Oid stashId = repository.QueryStashes().GetValue()[0].Id;

	fixture.Write("a.txt", "local edit\n");

	CHECK_FALSE(repository.StashPop(stashId).HasValue());
	CHECK(repository.QueryStashes().GetValue().size() == 1);
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "local edit\n");
}

TEST_CASE("Stashing untracked files keeps ignored ones, even inside an untracked directory")
{
	GitFixture fixture;
	fixture.SetIdentity("Tester", "tester@example.com");
	fixture.Write(".gitignore", "*.log\n");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Write("scratch/note.txt", "untracked\n");
	fixture.Write("scratch/build.log", "ignored inside an untracked directory\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<bool> saved = repository.StashSave("with untracked", UntrackedFiles::Included);
	REQUIRE(saved.HasValue());
	CHECK(saved.GetValue());

	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "scratch" / "note.txt"));
	REQUIRE(std::filesystem::exists(fixture.GetPath() / "scratch" / "build.log"));
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "scratch" / "build.log") == "ignored inside an untracked directory\n");

	const Core::Oid stashId = repository.QueryStashes().GetValue()[0].Id;
	REQUIRE(repository.StashApply(stashId).HasValue());
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "scratch" / "note.txt") == "untracked\n");
}

TEST_CASE("Stashing over unresolved conflicts fails with the reason and changes nothing")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);
	fixture.Branch("side");
	fixture.Write("a.txt", "ours\n");
	fixture.Commit("ours");
	fixture.Checkout("side");
	fixture.Write("a.txt", "theirs\n");
	fixture.Commit("theirs");
	fixture.Checkout("master");
	fixture.StartConflict("side");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const std::string before = ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt");

	const Result<bool> saved = repository.StashSave("over a conflict", UntrackedFiles::Excluded);
	REQUIRE_FALSE(saved.HasValue());
	CHECK(saved.GetError().Message.find("resolve or discard the conflicts first") != std::string::npos);
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == before);
	CHECK(repository.QueryStashes().GetValue().empty());
}

TEST_CASE("Popping a stash that conflicts with the branch keeps the stash and says so although libgit2 reports success")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);
	fixture.Branch("feature");
	fixture.Checkout("feature");
	fixture.Write("a.txt", "feature\n");
	fixture.Commit("feature");
	fixture.Checkout("master");
	fixture.Write("a.txt", "my edit\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	REQUIRE(repository.StashSave("on master", UntrackedFiles::Excluded).HasValue());
	const Core::Oid stashId = repository.QueryStashes().GetValue()[0].Id;
	const Core::CheckoutTarget featureBranch{.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"};
	REQUIRE_FALSE(repository.Checkout(featureBranch).Failure.has_value());

	const Result<void> popped = repository.StashPop(stashId);

	REQUIRE_FALSE(popped.HasValue());
	CHECK(popped.GetError().Message.find("kept as stash@{0}") != std::string::npos);
	CHECK(repository.QueryStashes().GetValue().size() == 1);

	const Result<Core::WorkingTreeStatus> status = repository.QueryStatus();
	REQUIRE(status.HasValue());
	REQUIRE(status.GetValue().Entries.size() == 1);
	CHECK(status.GetValue().Entries[0].Path == std::filesystem::path{"a.txt"});
	CHECK(status.GetValue().Entries[0].IsConflicted);
}

TEST_CASE("Stashing ends a merge, a cherry-pick or a revert, as git does, but not a rebase or a bisect")
{
	for (const UntrackedFiles untrackedFiles : {UntrackedFiles::Excluded, UntrackedFiles::Included})
	{
		const bool includeUntracked = untrackedFiles == UntrackedFiles::Included;
		CAPTURE(includeUntracked);

		GitFixture fixture;
		CommitOneFileAsTester(fixture);
		const std::string head = fixture.Commit("two");
		fixture.Write(".git/rebase-merge/head-name", "refs/heads/master\n");
		fixture.Write(".git/BISECT_LOG", "\n");
		fixture.Write(".git/MERGE_HEAD", head + "\n");
		fixture.Write(".git/MERGE_MSG", "\n");
		fixture.Write("a.txt", "changed\n");
		fixture.Write("new.txt", "fresh\n");

		Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

		const Result<bool> saved = repository.StashSave("mid-rebase", untrackedFiles);
		REQUIRE(saved.HasValue());
		CHECK(saved.GetValue());

		CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "one\n");
		CHECK(std::filesystem::exists(fixture.GetPath() / "new.txt") != includeUntracked);
		CHECK(std::filesystem::exists(fixture.GetPath() / ".git" / "rebase-merge" / "head-name"));
		CHECK(std::filesystem::exists(fixture.GetPath() / ".git" / "BISECT_LOG"));
		CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / ".git" / "MERGE_HEAD"));
		CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / ".git" / "MERGE_MSG"));
	}
}

TEST_CASE("Applying a stash that conflicts with the branch says so, and keeps the stash")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);
	fixture.Branch("feature");
	fixture.Checkout("feature");
	fixture.Write("a.txt", "feature\n");
	fixture.Commit("feature");
	fixture.Checkout("master");
	fixture.Write("a.txt", "my edit\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	REQUIRE(repository.StashSave("on master", UntrackedFiles::Excluded).HasValue());
	const Core::Oid stashId = repository.QueryStashes().GetValue()[0].Id;
	const Core::CheckoutTarget featureBranch{.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"};
	REQUIRE_FALSE(repository.Checkout(featureBranch).Failure.has_value());

	const Result<void> applied = repository.StashApply(stashId);

	REQUIRE_FALSE(applied.HasValue());
	CHECK(applied.GetError().Message.find("applied with conflicts in the index; the stash is kept") !=
		  std::string::npos);
	CHECK(repository.QueryStashes().GetValue().size() == 1);

	const Result<Core::WorkingTreeStatus> status = repository.QueryStatus();
	REQUIRE(status.HasValue());
	REQUIRE(status.GetValue().Entries.size() == 1);
	CHECK(status.GetValue().Entries[0].IsConflicted);
}

TEST_CASE("A log asked for a stash lists that stash alone and names the commit it was made on as its only parent")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);
	fixture.Write("a.txt", "changed\n");
	fixture.Write("new.txt", "untracked\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	REQUIRE(repository.StashSave("with untracked", UntrackedFiles::Included).HasValue());

	const Core::Oid stash = repository.QueryStashes().GetValue()[0].Id;
	const Core::Oid head = HeadCommitId(repository);

	const Result<Core::LogPage> page = repository.QueryLog(Core::LogRequest{.Stash = stash});
	REQUIRE(page.HasValue());

	const std::vector<Core::CommitSummary>& commits = page.GetValue().Commits;
	REQUIRE(commits.size() == 1);
	CHECK(commits[0].Id == stash);

	REQUIRE(commits[0].Parents.size() == 1);
	CHECK(commits[0].Parents[0] == head);

	CHECK(page.GetValue().Complete);
	CHECK(page.GetValue().Walked == 1);
	CHECK_FALSE(page.GetValue().UpstreamKnown);
}

TEST_CASE("A stash's diff holds its untracked files as added, which its commit's own does not")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);
	fixture.Write("a.txt", "changed\n");
	fixture.Write("new.txt", "untracked\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	REQUIRE(repository.StashSave("with untracked", UntrackedFiles::Included).HasValue());

	const Core::Oid stash = repository.QueryStashes().GetValue()[0].Id;

	using Changes = std::vector<ChangedPath>;

	CHECK(SortedChangedPathsOfStash(repository, stash, Core::DiffTarget::StashToParent) ==
		  Changes{{.Path = "a.txt", .Change = Core::FileChange::Modified},
				  {.Path = "new.txt", .Change = Core::FileChange::Added}});

	CHECK(SortedChangedPathsOfStash(repository, stash, Core::DiffTarget::CommitToParent) ==
		  Changes{{.Path = "a.txt", .Change = Core::FileChange::Modified}});
}

TEST_CASE("A stash made without untracked files diffs the same either way")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);
	fixture.Write("a.txt", "changed\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	REQUIRE(repository.StashSave("tracked only", UntrackedFiles::Excluded).HasValue());

	const Core::Oid stash = repository.QueryStashes().GetValue()[0].Id;

	const Result<Core::Diff> diff =
		repository.QueryDiff(Core::DiffRequest{.Target = Core::DiffTarget::StashToParent, .From = stash});
	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);
	CHECK(diff.GetValue().Files[0].Change == Core::FileChange::Modified);
}

TEST_CASE("A stash that took untracked files names the commit it keeps them in")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);
	fixture.Write("new.txt", "untracked\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	REQUIRE(repository.StashSave("untracked only", UntrackedFiles::Included).HasValue());

	const Core::Oid stash = repository.QueryStashes().GetValue()[0].Id;

	const Result<std::optional<Core::Oid>> untracked = repository.UntrackedOfStash(stash);
	REQUIRE(untracked.HasValue());
	REQUIRE(untracked.GetValue().has_value());

	const Result<std::string> text = repository.ReadBlob("new.txt", *untracked.GetValue());
	REQUIRE(text.HasValue());
	CHECK(text.GetValue().find("untracked") != std::string::npos);

	const Core::Oid plainCommit = HeadCommitId(repository);
	const Result<std::optional<Core::Oid>> untrackedOfPlainCommit = repository.UntrackedOfStash(plainCommit);
	REQUIRE(untrackedOfPlainCommit.HasValue());
	CHECK_FALSE(untrackedOfPlainCommit.GetValue().has_value());
}

TEST_CASE("Popping through a handle that has not seen the tree become clean succeeds")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);
	fixture.Write("a.txt", "changed\n");

	Core::Repository first = OpenRepositoryAt(fixture.GetPath());
	Core::Repository second = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(first.StashSave("work", UntrackedFiles::Excluded).HasValue());
	const Core::Oid stashId = first.QueryStashes().GetValue()[0].Id;
	FillIndexCache(second);

	fixture.Write("b.txt", "other\n");
	REQUIRE(first.Stage({"b.txt"}).HasValue());
	REQUIRE(first.Commit({.Message = "add b"}).HasValue());

	const Result<void> popped = second.StashPop(stashId);
	CHECK(popped.HasValue());
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "changed\n");
}

TEST_CASE("Stashing through a handle that has not seen a stage keeps the staged content as the stash's index")
{
	GitFixture fixture;
	CommitOneFileAsTester(fixture);

	Core::Repository staging = OpenRepositoryAt(fixture.GetPath());
	Core::Repository stashing = OpenRepositoryAt(fixture.GetPath());
	FillIndexCache(stashing);

	fixture.Write("a.txt", "staged\n");
	REQUIRE(staging.Stage({"a.txt"}).HasValue());
	fixture.Write("a.txt", "edited\n");

	REQUIRE(stashing.StashSave("work", UntrackedFiles::Excluded).HasValue());

	Core::Repository fresh = OpenRepositoryAt(fixture.GetPath());
	const Core::Oid stashId = fresh.QueryStashes().GetValue()[0].Id;
	REQUIRE(fresh.StashPop(stashId).HasValue());

	const Result<std::string> staged = fresh.ReadIndexBlob("a.txt");
	REQUIRE(staged.HasValue());
	CHECK(staged.GetValue() == "staged\n");
	CHECK(ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt") == "edited\n");
}
