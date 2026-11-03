#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"
#include "Git/WorktreeFixture.h"
#include "TempDirectory.h"

#include <filesystem>
#include <vector>

namespace {

	std::vector<Core::WorktreeEntry> ListWorktreesOf(const std::filesystem::path& openedAt)
	{
		const Core::Repository repository = OpenRepositoryAt(openedAt);
		Core::Result<std::vector<Core::WorktreeEntry>> listed = repository.QueryWorktrees();
		REQUIRE(listed.HasValue());
		return std::move(listed).GetValue();
	}

	void CommitOneFileAndBranchSide(GitFixture& fixture)
	{
		fixture.Write("a.txt", "one\n");
		fixture.Commit("one");
		fixture.Branch("side");
	}
}

TEST_CASE("The main checkout is listed first with the branch it has checked out")
{
	GitFixture fixture;
	CommitOneFileAndBranchSide(fixture);

	const std::vector<Core::WorktreeEntry> worktrees = ListWorktreesOf(fixture.GetPath());

	REQUIRE(worktrees.size() == 1);
	CHECK(worktrees[0].IsMain);
	CHECK(worktrees[0].Name.empty());
	CHECK(std::filesystem::equivalent(worktrees[0].Path, fixture.GetPath()));
	CHECK(worktrees[0].Head.Kind == Core::HeadKind::Branch);
	CHECK(worktrees[0].Head.BranchName == "master");
}

TEST_CASE("A linked worktree is listed after the main checkout with its folder, name and branch")
{
	GitFixture fixture;
	CommitOneFileAndBranchSide(fixture);

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(fixture, "side", worktreePath);

	const std::vector<Core::WorktreeEntry> worktrees = ListWorktreesOf(fixture.GetPath());

	REQUIRE(worktrees.size() == 2);
	CHECK(worktrees[0].IsMain);
	CHECK(!worktrees[1].IsMain);
	CHECK(worktrees[1].Name == "side-tree");
	CHECK(std::filesystem::equivalent(worktrees[1].Path, worktreePath));
	CHECK(worktrees[1].Head.BranchName == "side");
	CHECK(!worktrees[1].Locked);
	CHECK(!worktrees[1].FolderMissing);
}

TEST_CASE("A locked worktree is listed as locked with its reason")
{
	GitFixture fixture;
	CommitOneFileAndBranchSide(fixture);

	TempDirectory outside;
	AddWorktree(fixture, "side", outside.GetPath() / "side-tree");
	LockWorktree(fixture, "side-tree", "claude session");

	const std::vector<Core::WorktreeEntry> worktrees = ListWorktreesOf(fixture.GetPath());

	REQUIRE(worktrees.size() == 2);
	CHECK(worktrees[1].Locked);
	CHECK(worktrees[1].LockReason == "claude session");
}

TEST_CASE("A worktree whose folder was deleted by hand is listed as missing")
{
	GitFixture fixture;
	CommitOneFileAndBranchSide(fixture);

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(fixture, "side", worktreePath);
	std::filesystem::remove_all(worktreePath);

	const std::vector<Core::WorktreeEntry> worktrees = ListWorktreesOf(fixture.GetPath());

	REQUIRE(worktrees.size() == 2);
	CHECK(worktrees[1].FolderMissing);
	CHECK(!worktrees[1].Head.Commit.has_value());
}

TEST_CASE("A worktree with a detached HEAD is listed as detached")
{
	GitFixture fixture;
	CommitOneFileAndBranchSide(fixture);

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(fixture, "side", worktreePath);
	DetachHeadOf(worktreePath);

	const std::vector<Core::WorktreeEntry> worktrees = ListWorktreesOf(fixture.GetPath());

	REQUIRE(worktrees.size() == 2);
	CHECK(worktrees[1].Head.Kind == Core::HeadKind::Detached);
	CHECK(worktrees[1].Head.Commit.has_value());
}

TEST_CASE("A linked worktree of a bare repository is listed once, with no main checkout, which a bare repository lacks")
{
	GitFixture fixture;
	CommitOneFileAndBranchSide(fixture);

	TempDirectory outside;
	const std::filesystem::path barePath = outside.GetPath() / "bare.git";
	const std::filesystem::path worktreePath = outside.GetPath() / "linked";
	CloneBareWithWorktree(fixture, barePath, worktreePath);

	const std::vector<Core::WorktreeEntry> worktrees = ListWorktreesOf(worktreePath);

	REQUIRE(worktrees.size() == 1);
	CHECK(!worktrees[0].IsMain);
	CHECK(worktrees[0].Name == "linked");
	CHECK(std::filesystem::equivalent(worktrees[0].Path, worktreePath));
	CHECK(worktrees[0].Head.Kind == Core::HeadKind::Branch);
	CHECK(worktrees[0].Head.BranchName == "linked");
	CHECK(!worktrees[0].FolderMissing);
}

TEST_CASE("Asked from a linked worktree, the main checkout comes first just the same")
{
	GitFixture fixture;
	CommitOneFileAndBranchSide(fixture);

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(fixture, "side", worktreePath);

	const std::vector<Core::WorktreeEntry> worktrees = ListWorktreesOf(worktreePath);

	REQUIRE(worktrees.size() == 2);
	CHECK(worktrees[0].IsMain);
	CHECK(std::filesystem::equivalent(worktrees[0].Path, fixture.GetPath()));
	CHECK(!worktrees[1].IsMain);
	CHECK(std::filesystem::equivalent(worktrees[1].Path, worktreePath));
}
