#include <doctest/doctest.h>

#include "Core/Base/Glob.h"

using Core::GlobMatches;

TEST_CASE("A pattern with no wildcard matches only itself")
{
	CHECK(GlobMatches("Application.cpp", "Application.cpp"));
	CHECK_FALSE(GlobMatches("Application.cpp", "Application.h"));
}

TEST_CASE("A star stands for any run of characters")
{
	CHECK(GlobMatches("*.cpp", "Application.cpp"));
	CHECK(GlobMatches("App*", "Application.cpp"));
	CHECK(GlobMatches("*", "Application.cpp"));
	CHECK_FALSE(GlobMatches("*.cpp", "Application.h"));
}

TEST_CASE("A question mark stands for exactly one character")
{
	CHECK(GlobMatches("Ma?n.cpp", "Main.cpp"));
	CHECK_FALSE(GlobMatches("Ma?n.cpp", "Man.cpp"));
}

TEST_CASE("Several stars match without backtracking wrongly")
{
	// "*a*b*c" against a long name is where a naive matcher gives up or recurses too deep.
	CHECK(GlobMatches("*Git*Log*.cpp", "RepositoryGitLogReader.cpp"));
	CHECK_FALSE(GlobMatches("*Git*Log*.h", "RepositoryGitLogReader.cpp"));
}

TEST_CASE("Stars in a multi-segment pattern each stay inside their own segment")
{
	// Each segment is matched by the segment of the pattern facing it, so the pattern says
	// how deep it means and a star cannot quietly widen it.
	CHECK(GlobMatches("Core/*/Repository*.cpp", "Core/Git/RepositoryLog.cpp"));
	CHECK_FALSE(GlobMatches("Core/*/Repository*.cpp", "Core/src/Git/RepositoryLog.cpp"));
}

TEST_CASE("Matching ignores case")
{
	CHECK(GlobMatches("*.CPP", "Application.cpp"));
	CHECK(GlobMatches("application.cpp", "Application.cpp"));
}

TEST_CASE("A star does not cross a directory separator")
{
	// Otherwise "Core/*.cpp" would quietly mean "anywhere under Core", and a pattern could
	// not say how deep it means.
	CHECK_FALSE(GlobMatches("Core/*.cpp", "Core/Git/Repository.cpp"));
	CHECK(GlobMatches("Core/*.cpp", "Core/Repository.cpp"));
	CHECK_FALSE(GlobMatches("*.cpp", "Core/Repository.cpp"));
}

TEST_CASE("An empty pattern matches only empty text")
{
	CHECK(GlobMatches("", ""));
	CHECK_FALSE(GlobMatches("", "Application.cpp"));
}

TEST_CASE("A trailing star also matches nothing at all")
{
	CHECK(GlobMatches("Application.cpp*", "Application.cpp"));
}
