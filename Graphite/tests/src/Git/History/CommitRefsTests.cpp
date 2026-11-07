#include <doctest/doctest.h>

#include "GitFixtures.h"

#include "Git/History/CommitRefs.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/Palettes.h"

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

	Core::Tag TagAt(std::string name, Core::Oid target)
	{
		Core::Tag tag;
		tag.FullName = "refs/tags/" + name;
		tag.Name = std::move(name);
		tag.Target = target;
		return tag;
	}

	const std::vector<Core::Tag> kNoTags;
}

TEST_CASE("Commit refs: tags follow the branches on their commit, newest first, as tag pills")
{
	// Listed newest first, as the model lists them: release-b is newer than release-a.
	const Graphite::CommitRefMap refs = Graphite::BuildCommitRefs(
		{
			BranchAt("origin/main", OidWithFirstByte(1), Core::RefKind::Remote),
			BranchAt("main", OidWithFirstByte(1), Core::RefKind::Local, kIsHead),
			BranchAt("zeta", OidWithFirstByte(1)),
		},
		{TagAt("release-b", OidWithFirstByte(1)), TagAt("only-tag", OidWithFirstByte(2)), TagAt("release-a", OidWithFirstByte(1))});

	REQUIRE(refs.contains(OidWithFirstByte(1)));
	const std::vector<Graphite::CommitRef>& onFirst = refs.at(OidWithFirstByte(1));
	REQUIRE(onFirst.size() == 5);

	CHECK(onFirst[0].Kind == CommitRefKind::Head);
	CHECK(onFirst[1].Name == "zeta");
	CHECK(onFirst[2].Name == "origin/main");
	CHECK(onFirst[3].Name == "release-b");
	CHECK(onFirst[3].Kind == CommitRefKind::Tag);
	CHECK(onFirst[4].Name == "release-a");
	CHECK(onFirst[4].Kind == CommitRefKind::Tag);

	// A commit only a tag points at has its pill alone.
	REQUIRE(refs.contains(OidWithFirstByte(2)));
	REQUIRE(refs.at(OidWithFirstByte(2)).size() == 1);
	CHECK(refs.at(OidWithFirstByte(2))[0].Name == "only-tag");
	CHECK(refs.at(OidWithFirstByte(2))[0].Kind == CommitRefKind::Tag);
}

TEST_CASE("Commit refs: a tag and a branch with the same short name are two pills of two kinds")
{
	const Graphite::CommitRefMap refs =
		Graphite::BuildCommitRefs({BranchAt("x", OidWithFirstByte(1))}, {TagAt("x", OidWithFirstByte(1))});

	REQUIRE(refs.contains(OidWithFirstByte(1)));
	const std::vector<Graphite::CommitRef>& onCommit = refs.at(OidWithFirstByte(1));
	REQUIRE(onCommit.size() == 2);

	CHECK(onCommit[0].Name == "x");
	CHECK(onCommit[0].Kind == CommitRefKind::Local);
	CHECK(onCommit[1].Name == "x");
	CHECK(onCommit[1].Kind == CommitRefKind::Tag);
}

TEST_CASE("A commit no branch points at has no refs")
{
	const Graphite::CommitRefMap refs = Graphite::BuildCommitRefs({BranchAt("main", OidWithFirstByte(1))}, kNoTags);

	CHECK(refs.find(OidWithFirstByte(2)) == refs.end());
	CHECK(Graphite::BuildCommitRefs({}, kNoTags).empty());
}

TEST_CASE("Branches on one commit are grouped by commit and ordered head, local, remote, each group by name")
{
	const Graphite::CommitRefMap refs = Graphite::BuildCommitRefs({
		BranchAt("origin/main", OidWithFirstByte(1), Core::RefKind::Remote),
		BranchAt("zeta", OidWithFirstByte(1)),
		BranchAt("main", OidWithFirstByte(1), Core::RefKind::Local, kIsHead),
		BranchAt("alpha", OidWithFirstByte(1)),
		BranchAt("other", OidWithFirstByte(2)),
	}, kNoTags);

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
	}, kNoTags);

	REQUIRE(refs.contains(OidWithFirstByte(1)));
	const std::vector<Graphite::CommitRef>& refsOnCommit = refs.at(OidWithFirstByte(1));
	REQUIRE(refsOnCommit.size() == 1);
	CHECK(refsOnCommit[0].Name == "origin/main");
}

TEST_CASE("Git window: the checked-out branch's pill reads HEAD, an arrow and its name; other refs read their name")
{
	CHECK(Graphite::RefPillText(Graphite::CommitRef{.Name = "main", .Kind = CommitRefKind::Head}) ==
		  "HEAD \xE2\x86\x92 main");
	CHECK(Graphite::RefPillText(Graphite::CommitRef{.Name = "feature", .Kind = CommitRefKind::Local}) == "feature");
	CHECK(Graphite::RefPillText(Graphite::CommitRef{.Name = "origin/main", .Kind = CommitRefKind::Remote}) ==
		  "origin/main");
	CHECK(Graphite::RefPillText(Graphite::CommitRef{.Name = "v1.0", .Kind = CommitRefKind::Tag}) == "v1.0");
}

TEST_CASE("Git window: each kind of ref has its own pill colours, the head's on the accent")
{
	const Graphite::GitPalette palette = Graphite::ReadGitPalette(Graphite::DarkScheme());

	const Graphite::RefPillColours head = Graphite::RefPillColoursOf(CommitRefKind::Head, palette);
	const Graphite::RefPillColours local = Graphite::RefPillColoursOf(CommitRefKind::Local, palette);
	const Graphite::RefPillColours remote = Graphite::RefPillColoursOf(CommitRefKind::Remote, palette);
	const Graphite::RefPillColours tag = Graphite::RefPillColoursOf(CommitRefKind::Tag, palette);

	CHECK(head.Fill == palette.RefHeadFill);
	CHECK(local.Fill == palette.RefLocalFill);
	CHECK(remote.Fill == palette.RefRemoteFill);
	CHECK(tag.Fill == palette.RefTagFill);
	CHECK(head.Text == palette.RefHeadText);
	CHECK(local.Text == palette.RefLocalText);
	CHECK(remote.Text == palette.RefRemoteText);
	CHECK(tag.Text == palette.RefTagText);
	CHECK(local.Fill != remote.Fill);
	CHECK(remote.Fill != tag.Fill);
	CHECK(head.Fill != local.Fill);
}
