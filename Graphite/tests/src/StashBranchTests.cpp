#include <doctest/doctest.h>

#include "Git/StashBranch.h"

TEST_CASE("A stash made with a message names its branch after On")
{
	CHECK(Graphite::StashBranch("On main: half-done refactor") == "main");
}

TEST_CASE("A stash made without a message names its branch after WIP on")
{
	CHECK(Graphite::StashBranch("WIP on main: 1a2b3c4 Add the parser") == "main");
}

TEST_CASE("A branch name with a slash is kept whole")
{
	CHECK(Graphite::StashBranch("On feature/x: try this") == "feature/x");
	CHECK(Graphite::StashBranch("WIP on feature/x: 1a2b3c4 Summary: with a colon") == "feature/x");
}

TEST_CASE("A stash message without git's prefix names no branch")
{
	CHECK_FALSE(Graphite::StashBranch("just a message").has_value());
	CHECK_FALSE(Graphite::StashBranch("On main without a colon").has_value());
	CHECK_FALSE(Graphite::StashBranch("On : empty").has_value());
	CHECK_FALSE(Graphite::StashBranch("").has_value());
}

TEST_CASE("A message whose colon follows a space is not taken for a branch")
{
	CHECK_FALSE(Graphite::StashBranch("On second thought: keep this").has_value());
}
