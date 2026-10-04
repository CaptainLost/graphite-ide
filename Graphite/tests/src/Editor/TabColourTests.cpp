#include <doctest/doctest.h>

#include "Editor/TabColour.h"

namespace {

	using Graphite::GitDecoration;
	using Graphite::TabTextColour;

	constexpr ImU32 kStatus = IM_COL32(10, 20, 30, 200);
	constexpr ImU32 kDisabled = IM_COL32(1, 2, 3, 255);
}

TEST_CASE("A permanent undecorated tab keeps the theme's text colour")
{
	CHECK_FALSE(TabTextColour(GitDecoration::None, false, kStatus, kDisabled).has_value());
}

TEST_CASE("A preview undecorated tab is the disabled colour")
{
	const auto colour = TabTextColour(GitDecoration::None, true, kStatus, kDisabled);
	REQUIRE(colour.has_value());
	CHECK(*colour == kDisabled);
}

TEST_CASE("A permanent decorated tab takes the status colour unchanged")
{
	const auto colour = TabTextColour(GitDecoration::Modified, false, kStatus, kDisabled);
	REQUIRE(colour.has_value());
	CHECK(*colour == kStatus);
}

TEST_CASE("A preview decorated tab dims the status colour's alpha only")
{
	const auto colour = TabTextColour(GitDecoration::Modified, true, kStatus, kDisabled);
	REQUIRE(colour.has_value());
	CHECK(*colour == IM_COL32(10, 20, 30, 120));
}
