#include <doctest/doctest.h>

#include "Git/Content/ContentOrder.h"

#include "Git/Lines/LineReadFixtures.h"

#include <cstddef>
#include <optional>
#include <set>
#include <vector>

using namespace Graphite;

namespace {

	// Model.Branches holds every ref but the bases, in the order the refs were added.
	std::size_t BranchOfRef(const LineRead& read, std::size_t ref)
	{
		for (std::size_t branch = 0; branch < read.Model.Branches.size(); ++branch)
		{
			if (read.Model.Branches[branch].Ref == ref)
			{
				return branch;
			}
		}

		FAIL("the ref has no branch line");
		return 0;
	}

	void GiveOwnCommits(LineRead& read)
	{
		for (Core::BranchLine& line : read.Model.Branches)
		{
			line.Ahead = 1;
		}
	}
}

TEST_CASE("Git window: candidates are ordered shown first then in view then yours then remote")
{
	Fixtures::LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t line = builder.AddRemoteBranch("origin", "main", {.AgeDays = 0});
	builder.MakeBase(line);

	const std::size_t oldRemote = builder.AddRemoteBranch("origin", "old", {.AgeDays = 30});
	const std::size_t undated = builder.AddRemoteBranch("origin", "undated", {.AgeDays = std::nullopt});
	const std::size_t tieFirst = builder.AddRemoteBranch("origin", "tie-first", {.AgeDays = 10});
	const std::size_t tieSecond = builder.AddRemoteBranch("origin", "tie-second", {.AgeDays = 10});
	const std::size_t newRemote = builder.AddRemoteBranch("origin", "new", {.AgeDays = 3});
	const std::size_t oldMine = builder.AddLocal("old-mine", {.AgeDays = 20});
	const std::size_t newMine = builder.AddLocal("new-mine", {.AgeDays = 2});
	const std::size_t inView = builder.AddRemoteBranch("origin", "in-view", {.AgeDays = 40});
	const std::size_t shown = builder.AddRemoteBranch("origin", "shown", {.AgeDays = 50});

	const std::size_t merged = builder.AddLocal("merged", {.AgeDays = 1, .State = Core::BranchState::MergedByHistory});
	const std::size_t fresh = builder.AddLocal("fresh", {.AgeDays = 1, .State = Core::BranchState::NoOwnCommits});
	const std::size_t withoutOwn = builder.AddLocal("without-own", {.AgeDays = 1});
	const std::size_t lineless = builder.AddLocal("lineless", {.AgeDays = 1});

	for (const std::size_t ref : {oldRemote, undated, tieFirst, tieSecond, newRemote, oldMine, newMine, inView, shown, merged,
								  fresh, withoutOwn})
	{
		builder.Compare(ref, line);
	}

	LineRead read = builder.Build();
	GiveOwnCommits(read);
	read.Model.Branches[BranchOfRef(read, withoutOwn)].Ahead = 0;
	REQUIRE_FALSE(read.Model.Branches[BranchOfRef(read, lineless)].ComparedWith.has_value());

	const std::vector<std::size_t> order =
		OrderCandidates(read, std::set<std::size_t>{BranchOfRef(read, inView)}, BranchOfRef(read, shown));

	const std::vector<std::size_t> expected{
		BranchOfRef(read, shown),     BranchOfRef(read, inView),   BranchOfRef(read, newMine),
		BranchOfRef(read, oldMine),   BranchOfRef(read, newRemote), BranchOfRef(read, tieFirst),
		BranchOfRef(read, tieSecond), BranchOfRef(read, oldRemote), BranchOfRef(read, undated),
	};
	CHECK(order == expected);

	// Without a shown branch or rows in view, yours come before remote branches, and a branch with no tip time last.
	const std::vector<std::size_t> plainExpected{
		BranchOfRef(read, newMine),   BranchOfRef(read, oldMine),   BranchOfRef(read, newRemote),
		BranchOfRef(read, tieFirst),  BranchOfRef(read, tieSecond), BranchOfRef(read, oldRemote),
		BranchOfRef(read, inView),    BranchOfRef(read, shown),     BranchOfRef(read, undated),
	};
	CHECK(OrderCandidates(read, {}, std::nullopt) == plainExpected);

	// A shown branch that is no candidate is not checked.
	CHECK(OrderCandidates(read, {}, BranchOfRef(read, merged)) == plainExpected);
}
