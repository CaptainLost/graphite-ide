#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <string>

using Core::Branch;
using Core::RefKind;
using Core::Repository;

namespace {

	const Branch* FindBranchNamed(const std::vector<Branch>& branches, const std::string& name)
	{
		for (const Branch& candidate : branches)
		{
			if (candidate.Name == name)
			{
				return &candidate;
			}
		}

		return nullptr;
	}

	std::size_t CountHeads(const std::vector<Branch>& branches)
	{
		std::size_t headCount = 0;

		for (const Branch& branch : branches)
		{
			if (branch.IsHead)
			{
				++headCount;
			}
		}

		return headCount;
	}
}

TEST_CASE("Every local branch is listed with its tip")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string tip = fixture.Commit("add a");
	fixture.Branch("side");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<std::vector<Branch>> branches = repository.QueryBranches();

	REQUIRE(branches.HasValue());

	const Branch* side = FindBranchNamed(branches.GetValue(), "side");

	REQUIRE(side != nullptr);
	CHECK(side->Kind == RefKind::Local);
	CHECK(side->Tip.ToHex() == tip);
	CHECK_FALSE(side->IsHead);
}

TEST_CASE("The checked out branch is marked")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");
	fixture.Branch("side");
	fixture.Checkout("side");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<std::vector<Branch>> branches = repository.QueryBranches();

	REQUIRE(branches.HasValue());

	const Branch* side = FindBranchNamed(branches.GetValue(), "side");

	REQUIRE(side != nullptr);
	CHECK(side->IsHead);

	const std::size_t heads = CountHeads(branches.GetValue());

	CHECK(heads == 1);
}

TEST_CASE("A branch with no upstream has an unknown distance, so its zero counts do not claim it is in sync")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<std::vector<Branch>> branches = repository.QueryBranches();

	REQUIRE(branches.HasValue());
	REQUIRE_FALSE(branches.GetValue().empty());

	CHECK_FALSE(branches.GetValue().front().Upstream.has_value());
	CHECK(branches.GetValue().front().Ahead == 0);
	CHECK(branches.GetValue().front().Behind == 0);

	// DistanceKnown is also false for an upstream that resolves but cannot be measured; that
	// half needs a remote, which this fixture does not have.
	CHECK_FALSE(branches.GetValue().front().DistanceKnown);
}

TEST_CASE("Listing branches in a repository with no commits is empty rather than an error")
{
	GitFixture fixture;

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<std::vector<Branch>> branches = repository.QueryBranches();

	REQUIRE(branches.HasValue());
	CHECK(branches.GetValue().empty());
}
