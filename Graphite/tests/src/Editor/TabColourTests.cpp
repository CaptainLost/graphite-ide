#include <doctest/doctest.h>

#include "Editor/TabColour.h"

#include <optional>

namespace {

	using Graphite::GitDecoration;
	using Graphite::TabTextColour;

	constexpr ImU32 kStatus = IM_COL32(10, 20, 30, 200);
	constexpr ImU32 kDisabled = IM_COL32(1, 2, 3, 255);
	constexpr bool kPreviewTab = true;
	constexpr bool kPermanentTab = false;
}

TEST_CASE("A permanent undecorated tab keeps the theme's text colour")
{
	CHECK_FALSE(TabTextColour(GitDecoration::None, kPermanentTab, kStatus, kDisabled).has_value());
}

TEST_CASE("A preview undecorated tab is the disabled colour")
{
	const std::optional<ImU32> colour = TabTextColour(GitDecoration::None, kPreviewTab, kStatus, kDisabled);
	REQUIRE(colour.has_value());
	CHECK(*colour == kDisabled);
}

TEST_CASE("A permanent decorated tab takes the status colour unchanged")
{
	const std::optional<ImU32> colour = TabTextColour(GitDecoration::Modified, kPermanentTab, kStatus, kDisabled);
	REQUIRE(colour.has_value());
	CHECK(*colour == kStatus);
}

TEST_CASE("A preview decorated tab dims the status colour's alpha only")
{
	const std::optional<ImU32> colour = TabTextColour(GitDecoration::Modified, kPreviewTab, kStatus, kDisabled);
	REQUIRE(colour.has_value());
	CHECK(*colour == IM_COL32(10, 20, 30, 120));
}
