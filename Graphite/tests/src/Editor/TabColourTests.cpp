#include <doctest/doctest.h>

#include "Editor/TabColour.h"

#include <optional>

namespace {

	using Graphite::GitDecoration;
	using Graphite::TabDotColour;

	constexpr ImU32 kStatus = IM_COL32(10, 20, 30, 200);
}

TEST_CASE("A file with no git status has no dot on its tab")
{
	CHECK_FALSE(TabDotColour(GitDecoration::None, kStatus).has_value());
}

TEST_CASE("A modified file's tab shows a dot in the modified colour")
{
	const std::optional<ImU32> dot = TabDotColour(GitDecoration::Modified, kStatus);
	REQUIRE(dot.has_value());
	CHECK(*dot == kStatus);
}

TEST_CASE("Every git status puts its own colour, unchanged, on the dot")
{
	for (const GitDecoration decoration : {GitDecoration::Untracked, GitDecoration::Added, GitDecoration::Renamed,
										   GitDecoration::Modified, GitDecoration::Conflicted})
	{
		CAPTURE(static_cast<int>(decoration));
		CHECK(TabDotColour(decoration, kStatus) == std::optional<ImU32>{kStatus});
	}
}
