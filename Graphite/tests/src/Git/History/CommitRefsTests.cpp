#include <doctest/doctest.h>

#include "GitFixtures.h"

#include "Git/History/CommitRefs.h"

#include <string>
#include <utility>
#include <vector>

using Graphite::CommitRefKind;

namespace {

	constexpr bool kIsHead = true;

	Core::Branch BranchAt(std::string name, Core::Oid tip, Core::RefKind kind = Core::RefKind::Local,
						  bool isHead = false)
	{
		Core::Branch branch;
		branch.Name = std::move(name);
		branch.Tip = tip;
		branch.Kind = kind;
		branch.IsHead = isHead;
		return branch;
	}
}

TEST_CASE("A commit no branch points at has no refs")
{
	const Graphite::CommitRefMap refs = Graphite::BuildCommitRefs({BranchAt("main", OidWithFirstByte(1))});

	CHECK(refs.find(OidWithFirstByte(2)) == refs.end());
	CHECK(Graphite::BuildCommitRefs({}).empty());
}

TEST_CASE("Branches on one commit are grouped by commit and ordered head, local, remote, each group by name")
{
	const Graphite::CommitRefMap refs = Graphite::BuildCommitRefs({
		BranchAt("origin/main", OidWithFirstByte(1), Core::RefKind::Remote),
		BranchAt("zeta", OidWithFirstByte(1)),
		BranchAt("main", OidWithFirstByte(1), Core::RefKind::Local, kIsHead),
		BranchAt("alpha", OidWithFirstByte(1)),
		BranchAt("other", OidWithFirstByte(2)),
	});

	REQUIRE(refs.contains(OidWithFirstByte(1)));
	const std::vector<Graphite::CommitRef>& refsOnFirstCommit = refs.at(OidWithFirstByte(1));
	REQUIRE(refsOnFirstCommit.size() == 4);

	CHECK(refsOnFirstCommit[0].Name == "main");
	CHECK(refsOnFirstCommit[0].Kind == CommitRefKind::Head);
	CHECK(refsOnFirstCommit[1].Name == "alpha");
	CHECK(refsOnFirstCommit[1].Kind == CommitRefKind::Local);
	CHECK(refsOnFirstCommit[2].Name == "zeta");
	CHECK(refsOnFirstCommit[3].Name == "origin/main");
	CHECK(refsOnFirstCommit[3].Kind == CommitRefKind::Remote);

	REQUIRE(refs.contains(OidWithFirstByte(2)));
	CHECK(refs.at(OidWithFirstByte(2)).size() == 1);
}

TEST_CASE("Labels are kept from the front while they fit beside an empty summary, 128 wide between them with a gap after the last")
{
	const std::vector<float> widths{40.0f, 30.0f, 50.0f};

	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 200.0f, 0.0f, 84.0f) == 3);
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 132.0f, 0.0f, 84.0f) == 3);
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 131.0f, 0.0f, 84.0f) == 2);
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 78.0f, 0.0f, 84.0f) == 2);
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 77.0f, 0.0f, 84.0f) == 1);
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 43.0f, 0.0f, 84.0f) == 0);
}

TEST_CASE("A short summary keeps its whole width and a long one only its minimum, being shortened before any label is dropped")
{
	const std::vector<float> widths{40.0f, 30.0f};

	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 78.0f + 20.0f, 20.0f, 84.0f) == 2);
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 77.0f + 20.0f, 20.0f, 84.0f) == 1);

	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 78.0f + 84.0f, 900.0f, 84.0f) == 2);
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 77.0f + 84.0f, 900.0f, 84.0f) == 1);
}

TEST_CASE("No room, or no labels, fits nothing")
{
	CHECK(Graphite::RefLabelsThatFit({10.0f}, 4.0f, -5.0f, 0.0f, 84.0f) == 0);
	CHECK(Graphite::RefLabelsThatFit({10.0f}, 4.0f, 0.0f, 0.0f, 84.0f) == 0);
	CHECK(Graphite::RefLabelsThatFit({10.0f}, 4.0f, 50.0f, 900.0f, 84.0f) == 0);
	CHECK(Graphite::RefLabelsThatFit({}, 4.0f, 100.0f, 0.0f, 84.0f) == 0);
}

TEST_CASE("A remote's symbolic HEAD is not a label because the branch it points at is listed too")
{
	const Graphite::CommitRefMap refs = Graphite::BuildCommitRefs({
		BranchAt("origin/HEAD", OidWithFirstByte(1), Core::RefKind::Remote),
		BranchAt("origin/main", OidWithFirstByte(1), Core::RefKind::Remote),
	});

	REQUIRE(refs.contains(OidWithFirstByte(1)));
	const std::vector<Graphite::CommitRef>& refsOnCommit = refs.at(OidWithFirstByte(1));
	REQUIRE(refsOnCommit.size() == 1);
	CHECK(refsOnCommit[0].Name == "origin/main");
}