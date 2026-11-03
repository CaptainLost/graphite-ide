#pragma once

#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <git2.h>

#include <filesystem>
#include <string>

// Adds a worktree named branch + "-tree" at worktreePath, checked out on that existing branch.
inline void AddWorktree(const GitFixture& fixture, const std::string& branch, const std::filesystem::path& worktreePath)
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

// A bare clone of the fixture's repository at barePath, with a linked worktree named "linked" at worktreePath on a new
// branch of that name at the clone's HEAD.
inline void CloneBareWithWorktree(const GitFixture& fixture, const std::filesystem::path& barePath,
								  const std::filesystem::path& worktreePath)
{
	{
		Core::Result<Core::Repository> openedToInitialiseLibgit2 = Core::Repository::Open(fixture.GetPath());
		REQUIRE(openedToInitialiseLibgit2.HasValue());
	}

	git_clone_options cloneOptions = GIT_CLONE_OPTIONS_INIT;
	cloneOptions.bare = 1;

	git_repository* bareRepository = nullptr;
	REQUIRE(git_clone(&bareRepository, Core::ToUtf8(fixture.GetPath()).c_str(), Core::ToUtf8(barePath).c_str(),
					  &cloneOptions) == 0);
	REQUIRE(git_repository_is_bare(bareRepository) == 1);

	git_worktree_add_options addOptions = GIT_WORKTREE_ADD_OPTIONS_INIT;
	git_worktree* worktree = nullptr;
	REQUIRE(git_worktree_add(&worktree, bareRepository, "linked", Core::ToUtf8(worktreePath).c_str(), &addOptions) == 0);

	git_worktree_free(worktree);
	git_repository_free(bareRepository);
}

inline void DetachHeadOf(const std::filesystem::path& worktreePath)
{
	git_repository* worktreeRepository = nullptr;
	REQUIRE(git_repository_open(&worktreeRepository, Core::ToUtf8(worktreePath).c_str()) == 0);

	git_oid headCommit{};
	REQUIRE(git_reference_name_to_id(&headCommit, worktreeRepository, "HEAD") == 0);
	REQUIRE(git_repository_set_head_detached(worktreeRepository, &headCommit) == 0);

	git_repository_free(worktreeRepository);
}

inline void LockWorktree(const GitFixture& fixture, const std::string& worktreeName, const std::string& reason)
{
	git_repository* rawRepository = nullptr;
	REQUIRE(git_repository_open(&rawRepository, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);

	git_worktree* worktree = nullptr;
	REQUIRE(git_worktree_lookup(&worktree, rawRepository, worktreeName.c_str()) == 0);
	REQUIRE(git_worktree_lock(worktree, reason.c_str()) == 0);

	git_worktree_free(worktree);
	git_repository_free(rawRepository);
}
