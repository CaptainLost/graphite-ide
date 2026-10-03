#include <doctest/doctest.h>

#include "Git/History/CommitRefs.h"

#include <cstdint>
#include <string>
#include <vector>

using Graphite::CommitRefKind;

namespace {

	Core::Oid OidOf(std::uint8_t byte)
	{
		Core::Oid id;
		id.Bytes[0] = byte;
		return id;
	}

	Core::Branch BranchAt(std::string name, Core::Oid tip, Core::RefKind kind = Core::RefKind::Local,
						  bool head = false)
	{
		Core::Branch branch;
		branch.Name = std::move(name);
		branch.Tip = tip;
		branch.Kind = kind;
		branch.IsHead = head;
		return branch;
	}
}

TEST_CASE("A commit no branch points at has no refs")
{
	const auto refs = Graphite::BuildCommitRefs({BranchAt("main", OidOf(1))});

	CHECK(refs.find(OidOf(2)) == refs.end());
	CHECK(Graphite::BuildCommitRefs({}).empty());
}

TEST_CASE("Branches on one commit are grouped by commit and ordered head, local, remote")
{
	const auto refs = Graphite::BuildCommitRefs({
		BranchAt("origin/main", OidOf(1), Core::RefKind::Remote),
		BranchAt("zeta", OidOf(1)),
		BranchAt("main", OidOf(1), Core::RefKind::Local, true),
		BranchAt("alpha", OidOf(1)),
		BranchAt("other", OidOf(2)),
	});

	const auto found = refs.find(OidOf(1));
	REQUIRE(found != refs.end());
	REQUIRE(found->second.size() == 4);

	CHECK(found->second[0].Name == "main");
	CHECK(found->second[0].Kind == CommitRefKind::Head);
	CHECK(found->second[1].Name == "alpha");
	CHECK(found->second[1].Kind == CommitRefKind::Local);
	CHECK(found->second[2].Name == "zeta");
	CHECK(found->second[3].Name == "origin/main");
	CHECK(found->second[3].Kind == CommitRefKind::Remote);

	REQUIRE(refs.find(OidOf(2)) != refs.end());
	CHECK(refs.find(OidOf(2))->second.size() == 1);
}

TEST_CASE("Labels are kept from the front while they fit beside an empty summary")
{
	const std::vector<float> widths{40.0f, 30.0f, 50.0f};

	// The labels take 128 between them and a gap follows the last.
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 200.0f, 0.0f, 84.0f) == 3);
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 132.0f, 0.0f, 84.0f) == 3);
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 131.0f, 0.0f, 84.0f) == 2);
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 78.0f, 0.0f, 84.0f) == 2);
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 77.0f, 0.0f, 84.0f) == 1);
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 43.0f, 0.0f, 84.0f) == 0);
}

TEST_CASE("A short summary keeps its whole width and a long one only its minimum")
{
	const std::vector<float> widths{40.0f, 30.0f};

	// 78 for the labels and the gaps between and after them, plus what the summary keeps.
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 78.0f + 20.0f, 20.0f, 84.0f) == 2);
	CHECK(Graphite::RefLabelsThatFit(widths, 4.0f, 77.0f + 20.0f, 20.0f, 84.0f) == 1);

	// A summary far wider than the cell is shortened before any label is dropped.
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

TEST_CASE("A remote's symbolic HEAD is not a label")
{
	const auto refs = Graphite::BuildCommitRefs({
		BranchAt("origin/HEAD", OidOf(1), Core::RefKind::Remote),
		BranchAt("origin/main", OidOf(1), Core::RefKind::Remote),
	});

	const auto found = refs.find(OidOf(1));
	REQUIRE(found != refs.end());
	REQUIRE(found->second.size() == 1);
	CHECK(found->second[0].Name == "origin/main");
}