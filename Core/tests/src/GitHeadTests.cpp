#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <string>

using Core::HeadKind;
using Core::RepositoryOperation;
using Core::Repository;

namespace {

	Repository OpenAt(const std::filesystem::path& root)
	{
		auto opened = Repository::Open(root);
		REQUIRE(opened.HasValue());
		return std::move(opened).GetValue();
	}

	Core::HeadState HeadOf(const GitFixture& fixture)
	{
		const Repository repository = OpenAt(fixture.GetPath());
		auto head = repository.QueryHead();
		REQUIRE(head.HasValue());
		return head.GetValue();
	}

	// git_repository_state looks for these marker files and nothing else, so writing one is
	// how a test puts a repository in the middle of an operation without performing it.
	void Mark(GitFixture& fixture, const std::string& marker, const std::string& commit)
	{
		fixture.Write(".git/" + marker, commit + "\n");
	}
}

TEST_CASE("The head of a checked out branch names the branch and its commit")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string tip = fixture.Commit("add a");

	const Core::HeadState head = HeadOf(fixture);

	CHECK(head.Kind == HeadKind::Branch);
	CHECK(head.BranchName == "master");
	REQUIRE(head.Commit.has_value());
	CHECK(head.Commit->ToHex() == tip);
	CHECK(head.Operation == RepositoryOperation::None);
}

TEST_CASE("A detached head names its commit and no branch")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string tip = fixture.Commit("add a");
	fixture.Detach();

	const Core::HeadState head = HeadOf(fixture);

	CHECK(head.Kind == HeadKind::Detached);
	CHECK(head.BranchName.empty());
	REQUIRE(head.Commit.has_value());
	CHECK(head.Commit->ToHex() == tip);
}

TEST_CASE("A repository with no commits has an unborn head that still names its branch")
{
	GitFixture fixture;

	const Core::HeadState head = HeadOf(fixture);

	CHECK(head.Kind == HeadKind::Unborn);
	CHECK(head.BranchName == "master");
	CHECK_FALSE(head.Commit.has_value());
}

TEST_CASE("A merge stopped on a conflict reads as a merge in progress")
{
	GitFixture fixture;
	fixture.Write("a.txt", "base\n");
	fixture.Commit("base");
	fixture.Branch("side");
	fixture.Write("a.txt", "ours\n");
	fixture.Commit("ours");
	fixture.Checkout("side");
	fixture.Write("a.txt", "theirs\n");
	fixture.Commit("theirs");
	fixture.Checkout("master");
	fixture.StartConflict("side");

	CHECK(HeadOf(fixture).Operation == RepositoryOperation::Merge);
}

TEST_CASE("Each operation git can leave half done is named")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string tip = fixture.Commit("add a");

	SUBCASE("rebase")
	{
		fixture.Write(".git/rebase-merge/head-name", "refs/heads/master\n");
		CHECK(HeadOf(fixture).Operation == RepositoryOperation::Rebase);
	}

	SUBCASE("interactive rebase")
	{
		fixture.Write(".git/rebase-merge/interactive", "");
		CHECK(HeadOf(fixture).Operation == RepositoryOperation::Rebase);
	}

	SUBCASE("cherry-pick")
	{
		Mark(fixture, "CHERRY_PICK_HEAD", tip);
		CHECK(HeadOf(fixture).Operation == RepositoryOperation::CherryPick);
	}

	SUBCASE("revert")
	{
		Mark(fixture, "REVERT_HEAD", tip);
		CHECK(HeadOf(fixture).Operation == RepositoryOperation::Revert);
	}

	SUBCASE("bisect")
	{
		Mark(fixture, "BISECT_LOG", "# bad");
		CHECK(HeadOf(fixture).Operation == RepositoryOperation::Bisect);
	}

	SUBCASE("git am")
	{
		fixture.Write(".git/rebase-apply/applying", "");
		CHECK(HeadOf(fixture).Operation == RepositoryOperation::ApplyMailbox);
	}
}
