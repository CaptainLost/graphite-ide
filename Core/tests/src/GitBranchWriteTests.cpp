#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

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
