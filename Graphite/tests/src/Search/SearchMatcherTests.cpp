#include <doctest/doctest.h>

#include "Search/Matcher.h"

#include <vector>

namespace {

	std::vector<Graphite::SearchHit> Find(const Graphite::SearchQuery& query,
										  std::string_view text)
	{
		std::vector<Graphite::SearchHit> hits;
		Graphite::FindInText(query, text, "f.cpp", hits);

		return hits;
	}

	Graphite::SearchQuery Plain(std::string needle)
	{
		Graphite::SearchQuery query;
		query.Needle = std::move(needle);

		return query;
	}
}

TEST_CASE("FindInText reports the line, the text and the range")
{
	const std::vector<Graphite::SearchHit> hits = Find(Plain("beta"), "alpha\nbeta gamma\n");

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].Line == 1);
	CHECK(hits[0].Text == "beta gamma");
	CHECK(hits[0].From == 0);
	CHECK(hits[0].To == 4);
}

TEST_CASE("FindInText finds every occurrence on one line")
{
	const std::vector<Graphite::SearchHit> hits = Find(Plain("aa"), "aa bb aa");

	REQUIRE(hits.size() == 2);
	CHECK(hits[0].From == 0);
	CHECK(hits[1].From == 6);
}

// Overlapping matches are reported once each from the left, which is what every editor does:
// "aaa" contains "aa" twice by the letter and once to a reader.
TEST_CASE("FindInText does not report overlapping matches twice")
{
	const std::vector<Graphite::SearchHit> hits = Find(Plain("aa"), "aaa");

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].From == 0);
}

// Both directions matter: a fold that only lowers one side of the comparison (say, the needle
// but not the text) would pass a lowercase-needle-only test while still being wrong.
TEST_CASE("FindInText ignores case unless asked not to")
{
	CHECK(Find(Plain("beta"), "BETA").size() == 1);
	CHECK(Find(Plain("BETA"), "beta").size() == 1);
	CHECK(Find(Plain("BeTa"), "bEtA").size() == 1);

	Graphite::SearchQuery exact = Plain("beta");
	exact.MatchCase = true;

	CHECK(Find(exact, "BETA").empty());
	CHECK(Find(exact, "beta").size() == 1);

	Graphite::SearchQuery exactUpper = Plain("BETA");
	exactUpper.MatchCase = true;

	CHECK(Find(exactUpper, "beta").empty());
	CHECK(Find(exactUpper, "BETA").size() == 1);
}

TEST_CASE("Whole word rejects a match glued to an identifier character")
{
	Graphite::SearchQuery whole = Plain("count");
	whole.WholeWord = true;

	CHECK(Find(whole, "counter").empty());
	CHECK(Find(whole, "_count").empty());
	CHECK(Find(whole, "count9").empty());

	CHECK(Find(whole, "count").size() == 1);
	CHECK(Find(whole, "count(x)").size() == 1);
	CHECK(Find(whole, "a.count = 1").size() == 1);
}

TEST_CASE("FindInText handles the last line without a trailing newline")
{
	const std::vector<Graphite::SearchHit> hits = Find(Plain("end"), "a\nend");

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].Line == 1);
	CHECK(hits[0].Text == "end");
}

// A carriage return is part of the line as far as byte offsets go, but it must not end up in
// the text the results list draws.
TEST_CASE("FindInText strips a carriage return from the line it reports")
{
	const std::vector<Graphite::SearchHit> hits = Find(Plain("a"), "a\r\nb\r\n");

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].Text == "a");
}

TEST_CASE("An empty needle finds nothing")
{
	CHECK(Find(Plain(""), "anything at all").empty());
}

TEST_CASE("An empty mask accepts every name")
{
	CHECK(Graphite::MaskAccepts("", "a.cpp"));
	CHECK(Graphite::MaskAccepts("", "README"));
}

TEST_CASE("A mask accepts the names it lists and no others")
{
	CHECK(Graphite::MaskAccepts("*.cpp;*.h", "main.cpp"));
	CHECK(Graphite::MaskAccepts("*.cpp;*.h", "main.h"));
	CHECK_FALSE(Graphite::MaskAccepts("*.cpp;*.h", "main.hpp"));
	CHECK_FALSE(Graphite::MaskAccepts("*.cpp;*.h", "README.md"));

	CHECK(Graphite::MaskAccepts("premake5.lua", "premake5.lua"));
	CHECK(Graphite::MaskAccepts("a?c.txt", "abc.txt"));
	CHECK_FALSE(Graphite::MaskAccepts("a?c.txt", "ac.txt"));
}

// A mask is about names, so it must not be fooled by a path that happens to contain the text.
TEST_CASE("A mask compares the name it is given, extension and all")
{
	CHECK_FALSE(Graphite::MaskAccepts("*.h", "h"));
	CHECK(Graphite::MaskAccepts("*", "anything"));
}

TEST_CASE("A mask ignores case, because Windows paths do")
{
	CHECK(Graphite::MaskAccepts("*.CPP", "main.cpp"));
	CHECK(Graphite::MaskAccepts("*.cpp", "MAIN.CPP"));
}
