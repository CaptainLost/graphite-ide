#include <doctest/doctest.h>

#include "Core/Text/TextBuffer.h"
#include "Search/FileMatches.h"

namespace {

	Graphite::SearchQuery QueryFor(std::string needle)
	{
		Graphite::SearchQuery query;
		query.Needle = std::move(needle);

		return query;
	}
}

TEST_CASE("FindInBuffer reports matches as document offsets")
{
	const Core::TextBuffer buffer("alpha\nbeta\nalpha\n");

	const std::vector<Graphite::DocumentMatch> matches =
		Graphite::FindInBuffer(buffer, QueryFor("alpha"));

	REQUIRE(matches.size() == 2);

	CHECK(matches[0].From == 0);
	CHECK(matches[0].To == 5);
	CHECK(matches[1].From == 11);
	CHECK(matches[1].To == 16);
}

TEST_CASE("FindInBuffer finds two matches on one line")
{
	const Core::TextBuffer buffer("aa bb aa\n");

	const std::vector<Graphite::DocumentMatch> matches =
		Graphite::FindInBuffer(buffer, QueryFor("aa"));

	REQUIRE(matches.size() == 2);
	CHECK(matches[0].From == 0);
	CHECK(matches[1].From == 6);
}

TEST_CASE("FindInBuffer counts tabs as one byte each")
{
	const Core::TextBuffer buffer("\t\tvalue\n");

	const std::vector<Graphite::DocumentMatch> matches =
		Graphite::FindInBuffer(buffer, QueryFor("value"));

	REQUIRE(matches.size() == 1);
	CHECK(matches[0].From == 2);
}

TEST_CASE("FindInBuffer honours match case and whole word")
{
	const Core::TextBuffer buffer("Count counter count\n");

	Graphite::SearchQuery exact = QueryFor("count");
	exact.MatchCase = true;

	CHECK(Graphite::FindInBuffer(buffer, exact).size() == 2);

	Graphite::SearchQuery whole = QueryFor("count");
	whole.WholeWord = true;

	CHECK(Graphite::FindInBuffer(buffer, whole).size() == 2);

	Graphite::SearchQuery both = QueryFor("count");
	both.MatchCase = true;
	both.WholeWord = true;

	CHECK(Graphite::FindInBuffer(buffer, both).size() == 1);
}

TEST_CASE("FindInBuffer finds nothing for an empty needle")
{
	const Core::TextBuffer buffer("anything at all\n");

	CHECK(Graphite::FindInBuffer(buffer, QueryFor("")).empty());
}

TEST_CASE("NextMatch and PreviousMatch step and wrap")
{
	const std::vector<Graphite::DocumentMatch> matches{{0, 1}, {10, 11}, {20, 21}};

	CHECK(Graphite::NextMatch(matches, 0).value() == 1);
	CHECK(Graphite::NextMatch(matches, 1).value() == 2);

	CHECK(Graphite::NextMatch(matches, 2).value() == 0);

	CHECK(Graphite::PreviousMatch(matches, 2).value() == 1);
	CHECK(Graphite::PreviousMatch(matches, 0).value() == 2);
}

TEST_CASE("Stepping through no matches yields nothing rather than wrapping")
{
	const std::vector<Graphite::DocumentMatch> none;

	CHECK_FALSE(Graphite::NextMatch(none, 0).has_value());
	CHECK_FALSE(Graphite::PreviousMatch(none, 0).has_value());
}

TEST_CASE("MatchAtOrAfter finds the first match from an offset")
{
	const std::vector<Graphite::DocumentMatch> matches{{5, 6}, {15, 16}, {25, 26}};

	CHECK(Graphite::MatchAtOrAfter(matches, 0) == 0);
	CHECK(Graphite::MatchAtOrAfter(matches, 5) == 0);
	CHECK(Graphite::MatchAtOrAfter(matches, 6) == 1);
	CHECK(Graphite::MatchAtOrAfter(matches, 16) == 2);

	CHECK(Graphite::MatchAtOrAfter(matches, 99) == 0);
}

TEST_CASE("MatchAtOrAfter returns zero when there is nothing to find")
{
	const std::vector<Graphite::DocumentMatch> noMatches;

	CHECK(Graphite::MatchAtOrAfter(noMatches, 42) == 0);
}
