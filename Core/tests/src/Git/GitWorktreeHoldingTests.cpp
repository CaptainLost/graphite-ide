#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"
#include "TempDirectory.h"

#include <git2.h>

#include <filesystem>
#include <optional>
#include <string>

namespace {

	void AddWorktree(const GitFixture& fixture, const std::string& branch, const std::filesystem::path& worktreePath)
	{
		{
			Core::Result<Core::Repository> openedToInitialiseLibgit2 = Core::Repository::Open(fixture.GetPath());
			REQUIRE(openedToInitialiseLibgit2.HasValue());
		}

		git_repository* rawRepository = nullptr;
		REQUIRE(git_repository_open(&rawRepository, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);

		git_reference* branchReference = nullptr;
		REQUIRE(git_branch_lookup(&branchReference, rawRepository, branch.c_str(), GIT_BRANCH_LOCAL) == 0);

		git_worktree_add_options options = GIT_WORKTREE_ADD_OPTIONS_INIT;
		options.ref = branchReference;

		git_worktree* worktree = nullptr;
		const std::string worktreeName = branch + "-tree";
		REQUIRE(git_worktree_add(&worktree, rawRepository, worktreeName.c_str(), Core::ToUtf8(worktreePath).c_str(),
								 &options) == 0);

		git_worktree_free(worktree);
		git_reference_free(branchReference);
		git_repository_free(rawRepository);
	}

	void DetachHeadOf(const std::filesystem::path& worktreePath)
	{
		git_repository* worktreeRepository = nullptr;
		REQUIRE(git_repository_open(&worktreeRepository, Core::ToUtf8(worktreePath).c_str()) == 0);

		git_oid headCommit{};
		REQUIRE(git_reference_name_to_id(&headCommit, worktreeRepository, "HEAD") == 0);
		REQUIRE(git_repository_set_head_detached(worktreeRepository, &headCommit) == 0);

		git_repository_free(worktreeRepository);
	}

	std::filesystem::path HoldingPath(const Core::Repository& repository, const std::string& branchFullName)
	{
		const Core::Result<std::optional<std::filesystem::path>> holding = repository.WorktreeHolding(branchFullName);
		REQUIRE(holding.HasValue());
		REQUIRE(holding.GetValue().has_value());
		return *holding.GetValue();
	}

	bool IsHeldNowhere(const Core::Repository& repository, const std::string& branchFullName)
	{
		const Core::Result<std::optional<std::filesystem::path>> holding = repository.WorktreeHolding(branchFullName);
		REQUIRE(holding.HasValue());
		return !holding.GetValue().has_value();
	}
}

TEST_CASE("The branch checked out here is held by this working directory")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(std::filesystem::equivalent(HoldingPath(repository, "refs/heads/master"), fixture.GetPath()));
}

TEST_CASE("A branch checked out in a linked worktree is held by that worktree")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(fixture, "side", worktreePath);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(std::filesystem::equivalent(HoldingPath(repository, "refs/heads/side"), worktreePath));
}

TEST_CASE("A branch checked out nowhere is held by no working directory")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(IsHeldNowhere(repository, "refs/heads/side"));
}

TEST_CASE("A worktree with a detached HEAD holds no branch")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(fixture, "side", worktreePath);
	DetachHeadOf(worktreePath);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(IsHeldNowhere(repository, "refs/heads/side"));
}

TEST_CASE("Asked from a linked worktree the main checkout's branch is held by the main checkout")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(fixture, "side", worktreePath);

	Core::Repository fromWorktree = OpenRepositoryAt(worktreePath);

	CHECK(std::filesystem::equivalent(HoldingPath(fromWorktree, "refs/heads/master"), fixture.GetPath()));
	CHECK(std::filesystem::equivalent(HoldingPath(fromWorktree, "refs/heads/side"), worktreePath));
}
