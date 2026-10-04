#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <git2.h>

#include <algorithm>
#include <string>

namespace {

	Core::Repository OpenAt(const std::filesystem::path& root)
	{
		auto opened = Core::Repository::Open(root);
		REQUIRE(opened.HasValue());
		return std::move(opened).GetValue();
	}

	bool HasLocal(const Core::Repository& repository, const std::string& name)
	{
		auto branches = repository.QueryBranches();
		REQUIRE(branches.HasValue());
		return std::any_of(branches.GetValue().begin(), branches.GetValue().end(),
						   [&](const Core::Branch& branch) {
							   return branch.Kind == Core::RefKind::Local && branch.Name == name;
						   });
	}

	Core::Oid OidOf(const Core::Repository& repository)
	{
		auto head = repository.HeadCommit();
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

	Core::Repository repository = OpenAt(fixture.GetPath());
	const Core::Oid tip = OidOf(repository);

	REQUIRE(repository.CreateBranch("feature/x", tip).HasValue());

	CHECK(HasLocal(repository, "feature/x"));
	CHECK(repository.QueryHead().GetValue().BranchName == "master");
}

TEST_CASE("Creating a branch that exists fails")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	Core::Repository repository = OpenAt(fixture.GetPath());

	CHECK_FALSE(repository.CreateBranch("side", OidOf(repository)).HasValue());
}

TEST_CASE("A branch is renamed, including the checked out one")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	Core::Repository repository = OpenAt(fixture.GetPath());

	REQUIRE(repository.RenameBranch("side", "renamed").HasValue());
	CHECK(HasLocal(repository, "renamed"));
	CHECK_FALSE(HasLocal(repository, "side"));

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

	const Core::Repository repository = OpenAt(fixture.GetPath());

	CHECK(repository.CountUnmergedCommits("merged").GetValue() == 0);
	CHECK(repository.CountUnmergedCommits("ahead").GetValue() == 2);
}

TEST_CASE("A branch is deleted, but never the checked out one")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	Core::Repository repository = OpenAt(fixture.GetPath());

	REQUIRE(repository.DeleteBranch("side").HasValue());
	CHECK_FALSE(HasLocal(repository, "side"));

	CHECK_FALSE(repository.DeleteBranch("master").HasValue());
	CHECK(HasLocal(repository, "master"));
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

	const Core::Repository repository = OpenAt(fixture.GetPath());

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

	// origin/HEAD is a symbolic reference; branch iteration yields it and it resolves to the
	// tip of origin/master, which here is the commit "work" owns.
	git_repository* raw = nullptr;
	REQUIRE(git_repository_open(&raw, fixture.GetPath().string().c_str()) == 0);
	git_reference* symbolic = nullptr;
	REQUIRE(git_reference_symbolic_create(&symbolic, raw, "refs/remotes/origin/HEAD",
										  "refs/remotes/origin/master", 1, nullptr) == 0);
	git_reference_free(symbolic);
	git_repository_free(raw);

	const Core::Repository repository = OpenAt(fixture.GetPath());

	// origin/master holds "two", so "work" has nothing of its own whether or not the
	// symbolic origin/HEAD is also hidden.
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

	// Opening a Repository initialises libgit2, which the raw calls below rely on.
	{
		auto opened = Core::Repository::Open(fixture.GetPath());
		REQUIRE(opened.HasValue());
	}

	git_repository* raw = nullptr;
	REQUIRE(git_repository_open(&raw, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);

	git_reference* side = nullptr;
	REQUIRE(git_branch_lookup(&side, raw, "side", GIT_BRANCH_LOCAL) == 0);

	git_worktree_add_options options = GIT_WORKTREE_ADD_OPTIONS_INIT;
	options.ref = side;

	git_worktree* worktree = nullptr;
	REQUIRE(git_worktree_add(&worktree, raw, "side-tree", Core::ToUtf8(worktreePath).c_str(), &options) == 0);

	git_worktree_free(worktree);
	git_reference_free(side);
	git_repository_free(raw);

	Core::Repository repository = OpenAt(fixture.GetPath());
	auto deleted = repository.DeleteBranch("side");

	REQUIRE_FALSE(deleted.HasValue());
	CHECK(deleted.GetError().Message.starts_with("'side' is checked out in "));
	CHECK(deleted.GetError().Message.find("side-tree") != std::string::npos);
	CHECK(HasLocal(repository, "side"));
}

TEST_CASE("Deleting the branch checked out here keeps the plain refusal")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	Core::Repository repository = OpenAt(fixture.GetPath());
	auto deleted = repository.DeleteBranch("master");

	REQUIRE_FALSE(deleted.HasValue());
	CHECK(deleted.GetError().Message == "'master' is checked out and cannot be deleted");
}
