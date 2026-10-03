#include <doctest/doctest.h>

#include "Search/HitDisplay.h"

namespace {

	Graphite::SearchHit Line(std::string text, std::size_t from, std::size_t to)
	{
		Graphite::SearchHit hit;
		hit.File = "a.cpp";
		hit.Text = std::move(text);
		hit.From = from;
		hit.To = to;

		return hit;
	}
}

TEST_CASE("SplitForDisplay cuts a line around its match")
{
	const Graphite::SearchHit hit = Line("int value = 1;", 4, 9);
	const Graphite::HitLine split = Graphite::SplitForDisplay(hit);

	CHECK(split.Before == "int ");
	CHECK(split.Match == "value");
	CHECK(split.After == " = 1;");
}

// Every case here binds the hit to a named local before splitting it. A temporary would be a
// compile error, because HitLine holds views into the hit's own text and the rvalue overload is
// deleted to say so — which is how the first draft of these tests failed.
TEST_CASE("SplitForDisplay handles a match at the very start")
{
	const Graphite::SearchHit hit = Line("value = 1;", 0, 5);
	const Graphite::HitLine split = Graphite::SplitForDisplay(hit);

	CHECK(split.Before.empty());
	CHECK(split.Match == "value");
	CHECK(split.After == " = 1;");
}

TEST_CASE("SplitForDisplay handles a match running to the end")
{
	const Graphite::SearchHit hit = Line("call(value)", 5, 11);
	const Graphite::HitLine split = Graphite::SplitForDisplay(hit);

	CHECK(split.Before == "call(");
	CHECK(split.Match == "value)");
	CHECK(split.After.empty());
}

// The point of the trimming: a match three levels in should start at the left edge rather than
// sit behind an indentation the reader did not ask to see.
TEST_CASE("SplitForDisplay drops leading whitespace and moves the match with it")
{
	const Graphite::SearchHit hit = Line("\t\t\tint value = 1;", 7, 12);
	const Graphite::HitLine split = Graphite::SplitForDisplay(hit);

	CHECK(split.Before == "int ");
	CHECK(split.Match == "value");
	CHECK(split.After == " = 1;");
}

TEST_CASE("SplitForDisplay trims spaces as well as tabs")
{
	const Graphite::SearchHit hit = Line("    value", 4, 9);
	const Graphite::HitLine split = Graphite::SplitForDisplay(hit);

	CHECK(split.Before.empty());
	CHECK(split.Match == "value");
}

// Searching for whitespace finds it inside the indentation. Trimming the match away would leave
// the highlight over whatever followed it, which is worse than not trimming at all.
TEST_CASE("SplitForDisplay never trims past the match itself")
{
	const Graphite::SearchHit hit = Line("\t\tvalue", 1, 2);
	const Graphite::HitLine split = Graphite::SplitForDisplay(hit);

	// The indentation in front of the match still goes, which is the whole point of trimming.
	// What must survive is the match: the second tab is what was found, and it is still what the
	// highlight covers.
	CHECK(split.Before.empty());
	CHECK(split.Match == "\t");
	CHECK(split.After == "value");
}

TEST_CASE("SplitForDisplay leaves a line that is only whitespace alone")
{
	const Graphite::SearchHit hit = Line("\t\t", 0, 1);
	const Graphite::HitLine split = Graphite::SplitForDisplay(hit);

	CHECK(split.Match == "\t");
	CHECK(split.After == "\t");
}

TEST_CASE("SplitPathForDisplay strips the root and keeps the name apart")
{
	const Graphite::DisplayPath split = Graphite::SplitPathForDisplay(
		"D:/Repos/IDE/Core/src/Core/App/Application.cpp", "D:/Repos/IDE");

	CHECK(split.Name == "Application.cpp");

	// Backslashes, and a trailing one: the directory is drawn straight in front of the name, and
	// it should read the way every other path in this application does.
	CHECK(split.Directory == "Core\\src\\Core\\App\\");
}

TEST_CASE("SplitPathForDisplay leaves no directory for a file in the root")
{
	const Graphite::DisplayPath split =
		Graphite::SplitPathForDisplay("D:/Repos/IDE/CLAUDE.md", "D:/Repos/IDE");

	CHECK(split.Name == "CLAUDE.md");
	CHECK(split.Directory.empty());
}

// A relative path that climbs out through ".." is longer than the absolute one it replaces and
// harder to read, so a file outside the root keeps its full path.
TEST_CASE("SplitPathForDisplay keeps an absolute path for a file outside the root")
{
	const Graphite::DisplayPath split =
		Graphite::SplitPathForDisplay("C:/Elsewhere/other.cpp", "D:/Repos/IDE");

	CHECK(split.Name == "other.cpp");
	CHECK(split.Directory.find("..") == std::string::npos);
	CHECK(split.Directory.find("Elsewhere") != std::string::npos);
}

TEST_CASE("SplitPathForDisplay copes with an empty root")
{
	const Graphite::DisplayPath split =
		Graphite::SplitPathForDisplay("D:/Repos/IDE/a.cpp", "");

	CHECK(split.Name == "a.cpp");
	CHECK_FALSE(split.Directory.empty());
}
