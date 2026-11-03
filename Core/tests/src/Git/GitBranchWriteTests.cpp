#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <git2.h>

#include <string>

namespace {

	bool HasLocalBranch(const Core::Repository& repository, const std::string& name)
	{
		const Core::Result<std::vector<Core::Branch>> branches = repository.QueryBranches();
		REQUIRE(branches.HasValue());

		for (const Core::Branch& branch : branches.GetValue())
		{
			if (branch.Kind == Core::RefKind::Local && branch.Name == name)
			{
				return true;
			}
		}

		return false;
	}

	Core::Oid HeadCommitFromRepository(const Core::Repository& repository)
	{
		const Core::Result<Core::Oid> head = repository.HeadCommit();
		REQUIRE(head.HasValue());
		return head.GetValue();
	}
}

TEST_CASE("Branch names are validated the way git validates them")
{
	CHECK(Core::Repository::IsValidBranchName("feature/x"));
	CHECK(Core::Repository::IsValidBranchName("fix-123"));
	CHECK_FALSE(Core::Repository::IsValidBranchName(""));
	CHECK_FALSE(Core::Repository::IsValidBranchName("has space"));
	CHECK_FALSE(Core::Repository::IsValidBranchName("double..dot"));
	CHECK_FALSE(Core::Repository::IsValidBranchName("ends.lock"));
}

TEST_CASE("A branch is created at the commit it is given and does not move HEAD")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Oid tip = HeadCommitFromRepository(repository);

	REQUIRE(repository.CreateBranch("feature/x", tip).HasValue());

	CHECK(HasLocalBranch(repository, "feature/x"));
	CHECK(repository.QueryHead().GetValue().BranchName == "master");
}

TEST_CASE("Creating a branch that exists fails")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK_FALSE(repository.CreateBranch("side", HeadCommitFromRepository(repository)).HasValue());
}

TEST_CASE("A branch is renamed, including the checked out one")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.RenameBranch("side", "renamed").HasValue());
	CHECK(HasLocalBranch(repository, "renamed"));
	CHECK_FALSE(HasLocalBranch(repository, "side"));

	REQUIRE(repository.RenameBranch("master", "trunk").HasValue());
	CHECK(repository.QueryHead().GetValue().BranchName == "trunk");
}

TEST_CASE("A merged branch has no commits of its own, an unmerged one counts them")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("merged");
	fixture.Branch("ahead");
	fixture.Checkout("ahead");
	fixture.Write("a.txt", "two\n");
	fixture.Commit("two");
	fixture.Write("a.txt", "three\n");
	fixture.Commit("three");
	fixture.Checkout("master");

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(repository.CountUnmergedCommits("merged").GetValue() == 0);
	CHECK(repository.CountUnmergedCommits("ahead").GetValue() == 2);
}

TEST_CASE("A branch is deleted, but never the checked out one")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.DeleteBranch("side").HasValue());
	CHECK_FALSE(HasLocalBranch(repository, "side"));

	CHECK_FALSE(repository.DeleteBranch("master").HasValue());
	CHECK(HasLocalBranch(repository, "master"));
}

TEST_CASE("Commits a remote-tracking branch also holds are not counted as unmerged")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("work");
	fixture.Checkout("work");
	fixture.Write("a.txt", "two\n");
	const std::string tip = fixture.Commit("two");
	fixture.Checkout("master");
	fixture.PushTo("work", tip);

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(repository.CountUnmergedCommits("work").GetValue() == 0);
}

TEST_CASE("A symbolic origin/HEAD neither breaks the count nor changes it")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("work");
	fixture.Checkout("work");
	fixture.Write("a.txt", "two\n");
	const std::string tip = fixture.Commit("two");
	fixture.Checkout("master");
	fixture.PushTo("master", tip);

	// Branch iteration yields the symbolic origin/HEAD too, and it resolves to origin/master's
	// tip, which here is the commit "work" owns.
	git_repository* rawRepository = nullptr;
	REQUIRE(git_repository_open(&rawRepository, fixture.GetPath().string().c_str()) == 0);
	git_reference* symbolicReference = nullptr;
	const int overwriteExisting = 1;
	const char* const noLogMessage = nullptr;
	REQUIRE(git_reference_symbolic_create(&symbolicReference, rawRepository, "refs/remotes/origin/HEAD",
										  "refs/remotes/origin/master", overwriteExisting, noLogMessage) == 0);
	git_reference_free(symbolicReference);
	git_repository_free(rawRepository);

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(repository.CountUnmergedCommits("work").GetValue() == 0);
	CHECK(repository.CountUnmergedCommits("master").GetValue() == 0);
}

TEST_CASE("Deleting a branch another worktree holds names that worktree")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";

	{
		Core::Result<Core::Repository> openedToInitialiseLibgit2 = Core::Repository::Open(fixture.GetPath());
		REQUIRE(openedToInitialiseLibgit2.HasValue());
	}

	git_repository* rawRepository = nullptr;
	REQUIRE(git_repository_open(&rawRepository, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);

	git_reference* sideBranch = nullptr;
	REQUIRE(git_branch_lookup(&sideBranch, rawRepository, "side", GIT_BRANCH_LOCAL) == 0);

	git_worktree_add_options options = GIT_WORKTREE_ADD_OPTIONS_INIT;
	options.ref = sideBranch;

	git_worktree* worktree = nullptr;
	REQUIRE(git_worktree_add(&worktree, rawRepository, "side-tree", Core::ToUtf8(worktreePath).c_str(), &options) ==
			0);

	git_worktree_free(worktree);
	git_reference_free(sideBranch);
	git_repository_free(rawRepository);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	Core::Result<void> deleted = repository.DeleteBranch("side");

	REQUIRE_FALSE(deleted.HasValue());
	CHECK(deleted.GetError().Message.starts_with("'side' is checked out in "));
	CHECK(deleted.GetError().Message.find("side-tree") != std::string::npos);
	CHECK(HasLocalBranch(repository, "side"));
}

TEST_CASE("Deleting the branch checked out here keeps the plain refusal")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	Core::Result<void> deleted = repository.DeleteBranch("master");

	REQUIRE_FALSE(deleted.HasValue());
	CHECK(deleted.GetError().Message == "'master' is checked out and cannot be deleted");
}
