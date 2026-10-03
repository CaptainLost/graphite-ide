#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <algorithm>
#include <string>

using Core::Branch;
using Core::RefKind;
using Core::Repository;

namespace {

	Repository OpenAt(const std::filesystem::path& root)
	{
		auto opened = Repository::Open(root);
		REQUIRE(opened.HasValue());
		return std::move(opened).GetValue();
	}

	const Branch* Find(const std::vector<Branch>& branches, const std::string& name)
	{
		const auto entry = std::find_if(branches.begin(), branches.end(),
										[&name](const Branch& candidate) {
											return candidate.Name == name;
										});

		return entry == branches.end() ? nullptr : &*entry;
	}
}

TEST_CASE("Every local branch is listed with its tip")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string tip = fixture.Commit("add a");
	fixture.Branch("side");

	const Repository repository = OpenAt(fixture.GetPath());

	auto branches = repository.QueryBranches();

	REQUIRE(branches.HasValue());

	const Branch* side = Find(branches.GetValue(), "side");

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

	const Repository repository = OpenAt(fixture.GetPath());

	auto branches = repository.QueryBranches();

	REQUIRE(branches.HasValue());

	const Branch* side = Find(branches.GetValue(), "side");

	REQUIRE(side != nullptr);
	CHECK(side->IsHead);

	const auto heads = std::count_if(branches.GetValue().begin(), branches.GetValue().end(),
									 [](const Branch& branch) { return branch.IsHead; });

	CHECK(heads == 1);
}

TEST_CASE("A branch with no upstream reports no counts")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	auto branches = repository.QueryBranches();

	REQUIRE(branches.HasValue());
	REQUIRE_FALSE(branches.GetValue().empty());

	// Zero and zero would read as "in sync with the remote", which is a different and
	// wrong claim for a branch that was never pushed.
	CHECK_FALSE(branches.GetValue().front().Upstream.has_value());
	CHECK(branches.GetValue().front().Ahead == 0);
	CHECK(branches.GetValue().front().Behind == 0);

	// DistanceKnown also covers a resolved-but-unmeasurable upstream (a symbolic
	// upstream, or a failed graph walk) — that case needs a remote to reach and the
	// fixture has none, so only the no-upstream half of the contract is pinned here.
	CHECK_FALSE(branches.GetValue().front().DistanceKnown);
}

TEST_CASE("Listing branches in a repository with no commits is empty rather than an error")
{
	GitFixture fixture;

	const Repository repository = OpenAt(fixture.GetPath());

	auto branches = repository.QueryBranches();

	REQUIRE(branches.HasValue());
	CHECK(branches.GetValue().empty());
}
