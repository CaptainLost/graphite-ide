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
