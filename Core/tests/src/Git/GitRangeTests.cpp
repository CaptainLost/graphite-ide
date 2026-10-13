#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <string>

namespace {

	Core::Oid TipOf(const Core::Repository& repository, const std::string& fullName)
	{
		const Core::Result<Core::Oid> tip = repository.ResolveReference(fullName);
		REQUIRE(tip.HasValue());
		return tip.GetValue();
	}
}

TEST_CASE("A full reference name resolves to the commit it points at")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("one");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(TipOf(repository, "refs/heads/master").ToHex() == first);
}

TEST_CASE("A reference that does not exist resolves to NotFound")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::Oid> resolved = repository.ResolveReference("refs/heads/gone");

	REQUIRE_FALSE(resolved.HasValue());
	CHECK(resolved.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("The merge base of a branch and the branch it forked from is the fork point")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string forkPoint = fixture.Commit("one");
	fixture.Branch("side");

	fixture.Write("a.txt", "two\n");
	fixture.Commit("master moves on");

	fixture.Checkout("side");
	fixture.Write("b.txt", "side\n");
	fixture.Commit("side work");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::Oid> mergeBase =
		repository.MergeBase(TipOf(repository, "refs/heads/side"), TipOf(repository, "refs/heads/master"));

	REQUIRE(mergeBase.HasValue());
	CHECK(mergeBase.GetValue().ToHex() == forkPoint);
}

TEST_CASE("The merge base of a commit and its own descendant is the commit itself")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("one");
	fixture.Branch("side");

	fixture.Write("a.txt", "two\n");
	fixture.Commit("two");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::Oid> mergeBase =
		repository.MergeBase(TipOf(repository, "refs/heads/side"), TipOf(repository, "refs/heads/master"));

	REQUIRE(mergeBase.HasValue());
	CHECK(mergeBase.GetValue().ToHex() == first);
}

TEST_CASE("Two histories with no common ancestor have no merge base")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	fixture.StartOrphan("orphan");
	fixture.Write("b.txt", "unrelated\n");
	fixture.Commit("a root of its own");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<Core::Oid> mergeBase =
		repository.MergeBase(TipOf(repository, "refs/heads/orphan"), TipOf(repository, "refs/heads/master"));

	REQUIRE_FALSE(mergeBase.HasValue());
	CHECK(mergeBase.GetError().Code == Core::ErrorCode::NotFound);
}
