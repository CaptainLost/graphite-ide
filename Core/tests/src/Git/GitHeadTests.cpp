#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <string>
#include <utility>

using Core::HeadKind;
using Core::Repository;
using Core::RepositoryOperation;

namespace {

	Core::HeadState HeadOf(const GitFixture& fixture)
	{
		const Repository repository = OpenRepositoryAt(fixture.GetPath());
		const Core::Result<Core::HeadState> head = repository.QueryHead();
		REQUIRE(head.HasValue());
		return head.GetValue();
	}

	// git_repository_state looks only for marker files, so writing one puts a repository in the
	// middle of an operation without performing it.
	void WriteOperationMarker(GitFixture& fixture, const std::string& markerFile, const std::string& firstLine)
	{
		fixture.Write(".git/" + markerFile, firstLine + "\n");
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

TEST_CASE("A stopped rebase, which git leaves on a detached head, reads as both detached and rebasing")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string tip = fixture.Commit("add a");
	fixture.Detach();
	fixture.Write(".git/rebase-merge/head-name", "refs/heads/master\n");

	const Core::HeadState head = HeadOf(fixture);

	CHECK(head.Kind == HeadKind::Detached);
	CHECK(head.Operation == RepositoryOperation::Rebase);
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
		const std::string emptyMarker = "";
		fixture.Write(".git/rebase-merge/interactive", emptyMarker);
		CHECK(HeadOf(fixture).Operation == RepositoryOperation::Rebase);
	}

	SUBCASE("cherry-pick")
	{
		WriteOperationMarker(fixture, "CHERRY_PICK_HEAD", tip);
		CHECK(HeadOf(fixture).Operation == RepositoryOperation::CherryPick);
	}

	SUBCASE("revert")
	{
		WriteOperationMarker(fixture, "REVERT_HEAD", tip);
		CHECK(HeadOf(fixture).Operation == RepositoryOperation::Revert);
	}

	SUBCASE("bisect")
	{
		WriteOperationMarker(fixture, "BISECT_LOG", "# bad");
		CHECK(HeadOf(fixture).Operation == RepositoryOperation::Bisect);
	}

	SUBCASE("git am")
	{
		const std::string emptyMarker = "";
		fixture.Write(".git/rebase-apply/applying", emptyMarker);
		CHECK(HeadOf(fixture).Operation == RepositoryOperation::ApplyMailbox);
	}
}

TEST_CASE("A repository moved from refuses a call as not open rather than reaching libgit2")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Repository movedTo = std::move(repository);

	// NOLINTNEXTLINE(bugprone-use-after-move): the moved-from state is what is being tested.
	const Core::Result<Core::HeadState> head = repository.QueryHead();

	REQUIRE_FALSE(head.HasValue());
	CHECK(head.GetError().Code == Core::ErrorCode::NotFound);
	CHECK(head.GetError().Message == "the repository is not open");
}