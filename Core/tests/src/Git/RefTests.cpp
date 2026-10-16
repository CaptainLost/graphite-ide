#include <doctest/doctest.h>

#include "Core/Git/Ref.h"

#include <ostream>
#include <string>

TEST_CASE("A local branch's reference is its name under refs/heads")
{
	CHECK(Core::LocalBranchReference("feature/x") == "refs/heads/feature/x");
}

TEST_CASE("A branch's short name drops refs/heads or refs/remotes and leaves any other name whole")
{
	CHECK(Core::ShortBranchName("refs/heads/feature/x") == "feature/x");
	CHECK(Core::ShortBranchName("refs/remotes/origin/main") == "origin/main");
	CHECK(Core::ShortBranchName("HEAD") == "HEAD");
}