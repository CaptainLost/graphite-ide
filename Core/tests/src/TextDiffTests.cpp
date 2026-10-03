#include <doctest/doctest.h>

#include "Core/Git/TextDiff.h"

#include <vector>

using Core::LineHunk;

namespace {

	std::vector<LineHunk> Diff(std::string_view oldText, std::string_view newText)
	{
		auto hunks = Core::DiffTexts(oldText, newText);
		REQUIRE(hunks.HasValue());
		return std::move(hunks).GetValue();
	}
}

TEST_CASE("Two identical texts have no hunks")
{
	CHECK(Diff("a\nb\n", "a\nb\n").empty());
	CHECK(Diff("", "").empty());
}

TEST_CASE("An inserted line is a hunk with no old lines at the boundary it follows")
{
	CHECK(Diff("a\nb\n", "a\nx\nb\n") == std::vector<LineHunk>{{1, 0, 1, 1}});
}

TEST_CASE("A line appended at the end sits on the last boundary")
{
	CHECK(Diff("a\n", "a\nb\n") == std::vector<LineHunk>{{1, 0, 1, 1}});
}

TEST_CASE("A removed line is a hunk with no new lines")
{
	CHECK(Diff("a\nb\nc\n", "a\nc\n") == std::vector<LineHunk>{{1, 1, 1, 0}});
}

TEST_CASE("A line removed from the start sits on boundary zero")
{
	CHECK(Diff("a\nb\n", "b\n") == std::vector<LineHunk>{{0, 1, 0, 0}});
}

TEST_CASE("A changed line has lines on both sides")
{
	CHECK(Diff("a\nb\nc\n", "a\nB\nc\n") == std::vector<LineHunk>{{1, 1, 1, 1}});
}

TEST_CASE("Every line of a text diffed against nothing is new")
{
	CHECK(Diff("", "a\nb\n") == std::vector<LineHunk>{{0, 0, 0, 2}});
}

TEST_CASE("Separate changes are separate hunks in order")
{
	CHECK(Diff("1\n2\n3\n4\n5\n", "1\nX\n3\n4\nY\n") == std::vector<LineHunk>{{1, 1, 1, 1}, {4, 1, 4, 1}});
}

// git's own answer, pinned so it is a known behaviour: the last line differs in whether it
// ends, so it is reported as changed.
TEST_CASE("Adding a missing final newline changes the last line")
{
	CHECK(Diff("a\nb", "a\nb\n") == std::vector<LineHunk>{{1, 1, 1, 1}});
}
