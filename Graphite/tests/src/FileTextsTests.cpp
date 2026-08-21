#include <doctest/doctest.h>

#include "Git/FileTexts.h"

TEST_CASE("A successful read is present with its text")
{
	Core::Result<Graphite::SideText> side = Graphite::ReadSideText(std::string{"hello"});

	REQUIRE(side.HasValue());
	CHECK(side.GetValue().Present);
	CHECK(side.GetValue().Text == "hello");
}

TEST_CASE("An empty successful read is present and empty, not absent")
{
	// The distinction HasOld/HasNew exist for: a genuinely empty file is a real answer,
	// not the same as the file never having existed on that side.
	Core::Result<Graphite::SideText> side = Graphite::ReadSideText(std::string{});

	REQUIRE(side.HasValue());
	CHECK(side.GetValue().Present);
	CHECK(side.GetValue().Text.empty());
}

TEST_CASE("A NotFound read is absent without failing")
{
	Core::Result<Graphite::SideText> side =
		Graphite::ReadSideText(Core::Error{Core::ErrorCode::NotFound, "no such path in this tree"});

	REQUIRE(side.HasValue());
	CHECK(!side.GetValue().Present);
	CHECK(side.GetValue().Text.empty());
}

TEST_CASE("Any other error stays an error")
{
	Core::Result<Graphite::SideText> side =
		Graphite::ReadSideText(Core::Error{Core::ErrorCode::IoFailure, "disk fell over"});

	REQUIRE(!side.HasValue());
	CHECK(side.GetError().Code == Core::ErrorCode::IoFailure);
}

TEST_CASE("Unstaged changes compare the index against the disk")
{
	const Graphite::WorkingTreeSides sides = Graphite::SidesFor(Core::DiffTarget::WorkTreeToIndex);

	CHECK(sides.Old == Graphite::TextSide::Index);
	CHECK(sides.New == Graphite::TextSide::WorkingTree);
}

TEST_CASE("Staged changes compare HEAD against the index")
{
	const Graphite::WorkingTreeSides sides = Graphite::SidesFor(Core::DiffTarget::IndexToHead);

	CHECK(sides.Old == Graphite::TextSide::Head);
	CHECK(sides.New == Graphite::TextSide::Index);
}

TEST_CASE("Everything since the last commit compares HEAD against the disk")
{
	const Graphite::WorkingTreeSides sides = Graphite::SidesFor(Core::DiffTarget::WorkTreeToHead);

	CHECK(sides.Old == Graphite::TextSide::Head);
	CHECK(sides.New == Graphite::TextSide::WorkingTree);
}

TEST_CASE("A target that names no working tree has no side to read")
{
	const Graphite::WorkingTreeSides commit = Graphite::SidesFor(Core::DiffTarget::CommitToParent);

	CHECK(commit.Old == Graphite::TextSide::Absent);
	CHECK(commit.New == Graphite::TextSide::Absent);

	const Graphite::WorkingTreeSides range = Graphite::SidesFor(Core::DiffTarget::RevisionToRevision);

	CHECK(range.Old == Graphite::TextSide::Absent);
	CHECK(range.New == Graphite::TextSide::Absent);
}
