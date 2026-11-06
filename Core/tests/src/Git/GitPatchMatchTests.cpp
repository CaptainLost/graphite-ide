#include <doctest/doctest.h>

#include "Core/Git/PatchMatch.h"
#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

	constexpr std::size_t kPlentyOfBudget = 1000;

	Core::Oid OidOf(const std::string& hex)
	{
		const std::optional<Core::Oid> id = Core::Oid::FromHex(hex);
		REQUIRE(id.has_value());
		return *id;
	}

	Core::Repository OpenRepository(GitFixture& fixture)
	{
		Core::Result<Core::Repository> opened = Core::Repository::Open(fixture.GetPath());
		REQUIRE(opened.HasValue());
		return std::move(opened).GetValue();
	}

	Core::PatchMatch Match(GitFixture& fixture, const std::string& tip, const std::string& base,
						   std::size_t budget, Core::PatchIdCache& cache)
	{
		const Core::Repository repository = OpenRepository(fixture);

		Core::Result<Core::PatchMatch> match = repository.MatchPatches(OidOf(tip), OidOf(base), budget, cache);
		REQUIRE(match.HasValue());
		return match.GetValue();
	}

	std::optional<Core::PatchIdEntry> EntryOf(const std::vector<Core::PatchIdEntry>& entries, const Core::Oid& commit)
	{
		for (const Core::PatchIdEntry& entry : entries)
		{
			if (entry.Commit == commit)
			{
				return entry;
			}
		}

		return std::nullopt;
	}

	// Leaves HEAD on master, with `side` at the fork point. Returns the fork point.
	std::string ForkSide(GitFixture& fixture)
	{
		fixture.Write("a.txt", "one\n");
		const std::string fork = fixture.Commit("one");
		fixture.Branch("side");
		return fork;
	}
}

TEST_CASE("A branch whose commits are all in the base has nothing of its own")
{
	GitFixture fixture;
	const std::string fork = ForkSide(fixture);
	fixture.Write("b.txt", "x\n");
	const std::string masterTip = fixture.Commit("master adds b");

	Core::PatchIdCache cache;

	CHECK(Match(fixture, fork, masterTip, kPlentyOfBudget, cache) == Core::PatchMatch{0, 1, 0, 0, true});
}

TEST_CASE("A commit with the same change on both sides counts as in the base")
{
	GitFixture fixture;
	ForkSide(fixture);
	fixture.Write("b.txt", "x\n");
	const std::string masterTip = fixture.Commit("master adds b");
	fixture.Checkout("side");
	fixture.Write("b.txt", "x\n");
	const std::string sideTip = fixture.Commit("side adds b");

	Core::PatchIdCache cache;

	const Core::PatchMatch expected{
		.Ahead = 1,
		.Behind = 1,
		.OwnChanges = 1,
		.InBase = 1,
		.Complete = true,
		.Twins = {Core::PatchTwin{.Own = OidOf(sideTip), .Twin = OidOf(masterTip)}},
	};

	CHECK(Match(fixture, sideTip, masterTip, kPlentyOfBudget, cache) == expected);
}

TEST_CASE("A commit carried over with an edit does not count as in the base")
{
	GitFixture fixture;
	ForkSide(fixture);
	fixture.Write("b.txt", "x\n");
	const std::string masterTip = fixture.Commit("master adds b");
	fixture.Checkout("side");
	fixture.Write("b.txt", "y\n");
	const std::string sideTip = fixture.Commit("side adds b differently");

	Core::PatchIdCache cache;
	const Core::PatchMatch match = Match(fixture, sideTip, masterTip, kPlentyOfBudget, cache);

	CHECK(match.OwnChanges == 1);
	CHECK(match.InBase == 0);
}

TEST_CASE("A merge on the branch is ahead but is not a change of its own")
{
	GitFixture fixture;
	ForkSide(fixture);
	fixture.Write("b.txt", "x\n");
	const std::string masterTip = fixture.Commit("master adds b");
	fixture.Checkout("side");
	fixture.Write("c.txt", "z\n");
	fixture.Commit("side adds c");
	const std::string mergeTip = fixture.MergeCommit("merge", "master");

	Core::PatchIdCache cache;
	const Core::PatchMatch match = Match(fixture, mergeTip, masterTip, kPlentyOfBudget, cache);

	CHECK(match.Ahead == 2);
	CHECK(match.OwnChanges == 1);
}

TEST_CASE("Past the budget the comparison stops and says it is incomplete")
{
	GitFixture fixture;
	ForkSide(fixture);
	fixture.Write("m1.txt", "1\n");
	fixture.Commit("m1");
	fixture.Write("m2.txt", "2\n");
	fixture.Commit("m2");
	fixture.Write("m3.txt", "3\n");
	const std::string masterTip = fixture.Commit("m3");
	fixture.Checkout("side");
	fixture.Write("s.txt", "s\n");
	const std::string sideTip = fixture.Commit("s");

	Core::PatchIdCache cache;
	const Core::PatchMatch match = Match(fixture, sideTip, masterTip, 2, cache);

	CHECK_FALSE(match.Complete);
	CHECK(match.Behind == 3);
}

TEST_CASE("A branch with no change of its own reads none of the base's commits")
{
	GitFixture fixture;
	const std::string fork = ForkSide(fixture);
	fixture.Write("m1.txt", "1\n");
	fixture.Commit("m1");
	fixture.Write("m2.txt", "2\n");
	fixture.Commit("m2");
	fixture.Write("m3.txt", "3\n");
	const std::string masterTip = fixture.Commit("m3");

	Core::PatchIdCache cache;
	const Core::PatchMatch match = Match(fixture, fork, masterTip, 1, cache);

	CHECK(match == Core::PatchMatch{0, 3, 0, 0, true});
	CHECK(cache.GetSize() == 0);
}

TEST_CASE("The base's commits are read only until every change of the branch has a twin")
{
	GitFixture fixture;
	ForkSide(fixture);
	fixture.Write("m1.txt", "1\n");
	fixture.Commit("m1");
	fixture.Write("m2.txt", "2\n");
	fixture.Commit("m2");
	fixture.Write("b.txt", "x\n");
	const std::string masterTip = fixture.Commit("master adds b");
	fixture.Checkout("side");
	fixture.Write("b.txt", "x\n");
	const std::string sideTip = fixture.Commit("side adds b");

	Core::PatchIdCache cache;
	const Core::PatchMatch match = Match(fixture, sideTip, masterTip, 1, cache);

	const Core::PatchMatch expected{
		.Ahead = 1,
		.Behind = 3,
		.OwnChanges = 1,
		.InBase = 1,
		.Complete = true,
		.Twins = {Core::PatchTwin{.Own = OidOf(sideTip), .Twin = OidOf(masterTip)}},
	};

	CHECK(match == expected);
	CHECK(cache.GetSize() == 2);
}

TEST_CASE("A commit's patch id is worked out once")
{
	GitFixture fixture;
	ForkSide(fixture);
	fixture.Write("b.txt", "x\n");
	const std::string masterTip = fixture.Commit("master adds b");
	fixture.Checkout("side");
	fixture.Write("c.txt", "z\n");
	const std::string sideTip = fixture.Commit("side adds c");

	Core::PatchIdCache cache;
	Match(fixture, sideTip, masterTip, kPlentyOfBudget, cache);

	const std::size_t afterFirst = cache.GetSize();
	CHECK(afterFirst == 2);

	Match(fixture, sideTip, masterTip, kPlentyOfBudget, cache);

	CHECK(cache.GetSize() == afterFirst);
}

TEST_CASE("A commit the repository does not hold is NotFound")
{
	GitFixture fixture;
	ForkSide(fixture);
	fixture.Write("b.txt", "x\n");
	const std::string head = fixture.Commit("two");

	Core::Oid missing = OidOf(head);

	for (std::uint8_t& byte : missing.Bytes)
	{
		byte = static_cast<std::uint8_t>(~byte);
	}

	const Core::Repository repository = OpenRepository(fixture);
	Core::PatchIdCache cache;
	const Core::Result<Core::PatchMatch> match = repository.MatchPatches(missing, OidOf(head), kPlentyOfBudget, cache);

	REQUIRE_FALSE(match.HasValue());
	CHECK(match.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("Patches: a match names each own commit's twin")
{
	GitFixture fixture;
	ForkSide(fixture);
	fixture.Checkout("side");
	fixture.Write("b.txt", "x\n");
	const std::string sideAddsB = fixture.Commit("side adds b");
	fixture.Write("c.txt", "z\n");
	const std::string sideAddsC = fixture.Commit("side adds c");

	fixture.Checkout("master");
	fixture.Write("m.txt", "m\n");
	fixture.Commit("master goes on");
	fixture.Write("b.txt", "x\n");
	const std::string masterAddsB = fixture.Commit("master adds b");
	fixture.Write("c.txt", "z\n");
	const std::string masterAddsC = fixture.Commit("master adds c");

	Core::PatchIdCache cache;
	const Core::PatchMatch match = Match(fixture, sideAddsC, masterAddsC, kPlentyOfBudget, cache);

	const std::vector<Core::PatchTwin> newestOwnCommitFirst{
		Core::PatchTwin{.Own = OidOf(sideAddsC), .Twin = OidOf(masterAddsC)},
		Core::PatchTwin{.Own = OidOf(sideAddsB), .Twin = OidOf(masterAddsB)},
	};

	CHECK(match.Twins == newestOwnCommitFirst);
}

TEST_CASE("Patches: a missing blob stops the walk as incomplete")
{
	GitFixture fixture;
	ForkSide(fixture);
	fixture.Write("m.txt", "only on master\n");
	const std::string masterTip = fixture.Commit("master adds m");
	fixture.Checkout("side");
	fixture.Write("b.txt", "x\n");
	const std::string sideTip = fixture.Commit("side adds b");

	fixture.RemoveObject(BlobHexOf("only on master\n"));

	const Core::Repository repository = OpenRepository(fixture);
	Core::PatchIdCache cache;
	const Core::Result<Core::PatchMatch> match = repository.MatchPatches(OidOf(sideTip), OidOf(masterTip), kPlentyOfBudget, cache);

	REQUIRE(match.HasValue());
	CHECK(match.GetValue().MissingObjects);
	CHECK_FALSE(match.GetValue().Complete);
	CHECK(match.GetValue().OwnChanges == 1);
	CHECK(match.GetValue().InBase == 0);
}

TEST_CASE("Patches: a cache lists each commit it holds with its patch id")
{
	const Core::Oid commitWithPatch = OidOf("1111111111111111111111111111111111111111");
	const Core::Oid patchId = OidOf("2222222222222222222222222222222222222222");
	const Core::Oid mergeWithoutPatch = OidOf("3333333333333333333333333333333333333333");
	const std::optional<Core::Oid> noPatch;

	Core::PatchIdCache cache;
	cache.Store(commitWithPatch, patchId);
	cache.Store(mergeWithoutPatch, noPatch);

	const std::vector<Core::PatchIdEntry> entries = cache.Entries();

	CHECK(entries.size() == 2);
	CHECK(EntryOf(entries, commitWithPatch) == Core::PatchIdEntry{.Commit = commitWithPatch, .PatchId = patchId});
	CHECK(EntryOf(entries, mergeWithoutPatch) == Core::PatchIdEntry{.Commit = mergeWithoutPatch, .PatchId = noPatch});
}
