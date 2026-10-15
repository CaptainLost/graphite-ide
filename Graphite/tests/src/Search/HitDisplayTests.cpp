#include <doctest/doctest.h>

#include "Search/HitDisplay.h"

namespace {

	Graphite::SearchHit HitWithText(std::string text, std::size_t from, std::size_t to)
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
	const Graphite::SearchHit hit = HitWithText("int value = 1;", 4, 9);
	const Graphite::HighlightedLabel displayLine = Graphite::SplitForDisplay(hit);

	CHECK(displayLine.Before == "int ");
	CHECK(displayLine.Match == "value");
	CHECK(displayLine.After == " = 1;");
}

TEST_CASE("SplitForDisplay handles a match at the very start")
{
	const Graphite::SearchHit hit = HitWithText("value = 1;", 0, 5);
	const Graphite::HighlightedLabel displayLine = Graphite::SplitForDisplay(hit);

	CHECK(displayLine.Before.empty());
	CHECK(displayLine.Match == "value");
	CHECK(displayLine.After == " = 1;");
}

TEST_CASE("SplitForDisplay handles a match running to the end")
{
	const Graphite::SearchHit hit = HitWithText("call(value)", 5, 11);
	const Graphite::HighlightedLabel displayLine = Graphite::SplitForDisplay(hit);

	CHECK(displayLine.Before == "call(");
	CHECK(displayLine.Match == "value)");
	CHECK(displayLine.After.empty());
}

TEST_CASE("SplitForDisplay drops leading whitespace and moves the match with it")
{
	const Graphite::SearchHit hit = HitWithText("\t\t\tint value = 1;", 7, 12);
	const Graphite::HighlightedLabel displayLine = Graphite::SplitForDisplay(hit);

	CHECK(displayLine.Before == "int ");
	CHECK(displayLine.Match == "value");
	CHECK(displayLine.After == " = 1;");
}

TEST_CASE("SplitForDisplay trims spaces as well as tabs")
{
	const Graphite::SearchHit hit = HitWithText("    value", 4, 9);
	const Graphite::HighlightedLabel displayLine = Graphite::SplitForDisplay(hit);

	CHECK(displayLine.Before.empty());
	CHECK(displayLine.Match == "value");
}

TEST_CASE("SplitForDisplay never trims past the match itself")
{
	const Graphite::SearchHit hit = HitWithText("\t\tvalue", 1, 2);
	const Graphite::HighlightedLabel displayLine = Graphite::SplitForDisplay(hit);

	CHECK(displayLine.Before.empty());
	CHECK(displayLine.Match == "\t");
	CHECK(displayLine.After == "value");
}

TEST_CASE("SplitForDisplay leaves a line that is only whitespace alone")
{
	const Graphite::SearchHit hit = HitWithText("\t\t", 0, 1);
	const Graphite::HighlightedLabel displayLine = Graphite::SplitForDisplay(hit);

	CHECK(displayLine.Match == "\t");
	CHECK(displayLine.After == "\t");
}

TEST_CASE("SplitPathForDisplay strips the root and keeps the name apart")
{
	const Graphite::DisplayPath displayPath = Graphite::SplitPathForDisplay(
		"D:/Repos/IDE/Core/src/Core/App/Application.cpp", "D:/Repos/IDE");

	CHECK(displayPath.Name == "Application.cpp");
	CHECK(displayPath.Directory == "Core\\src\\Core\\App\\");
}

TEST_CASE("SplitPathForDisplay leaves no directory for a file in the root")
{
	const Graphite::DisplayPath displayPath =
		Graphite::SplitPathForDisplay("D:/Repos/IDE/CLAUDE.md", "D:/Repos/IDE");

	CHECK(displayPath.Name == "CLAUDE.md");
	CHECK(displayPath.Directory.empty());
}

TEST_CASE("SplitPathForDisplay keeps an absolute path for a file outside the root")
{
	const Graphite::DisplayPath displayPath =
		Graphite::SplitPathForDisplay("C:/Elsewhere/other.cpp", "D:/Repos/IDE");

	CHECK(displayPath.Name == "other.cpp");
	CHECK(displayPath.Directory.find("..") == std::string::npos);
	CHECK(displayPath.Directory.find("Elsewhere") != std::string::npos);
}

TEST_CASE("SplitPathForDisplay copes with an empty root")
{
	const std::filesystem::path emptyRoot = "";

	const Graphite::DisplayPath displayPath =
		Graphite::SplitPathForDisplay("D:/Repos/IDE/a.cpp", emptyRoot);

	CHECK(displayPath.Name == "a.cpp");
	CHECK_FALSE(displayPath.Directory.empty());
}
