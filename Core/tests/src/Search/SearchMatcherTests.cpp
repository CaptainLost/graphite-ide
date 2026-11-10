#include <doctest/doctest.h>

#include "Core/Search/Matcher.h"

#include <vector>

namespace {

	std::vector<Core::SearchHit> Find(const Core::SearchQuery& query,
										  std::string_view text)
	{
		std::vector<Core::SearchHit> hits;
		Core::FindInText(query, text, "f.cpp", hits);

		return hits;
	}

	Core::SearchQuery Plain(std::string needle)
	{
		Core::SearchQuery query;
		query.Needle = std::move(needle);

		return query;
	}
}

TEST_CASE("FindInText reports the line, the text and the range")
{
	const std::vector<Core::SearchHit> hits = Find(Plain("beta"), "alpha\nbeta gamma\n");

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].Line == 1);
	CHECK(hits[0].Text == "beta gamma");
	CHECK(hits[0].From == 0);
	CHECK(hits[0].To == 4);
}

TEST_CASE("FindInText finds every occurrence on one line")
{
	const std::vector<Core::SearchHit> hits = Find(Plain("aa"), "aa bb aa");

	REQUIRE(hits.size() == 2);
	CHECK(hits[0].From == 0);
	CHECK(hits[1].From == 6);
}

TEST_CASE("FindInText reports overlapping matches once each from the left")
{
	const std::vector<Core::SearchHit> hits = Find(Plain("aa"), "aaa");

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].From == 0);
}

TEST_CASE("FindInText ignores case in both directions unless asked not to")
{
	CHECK(Find(Plain("beta"), "BETA").size() == 1);
	CHECK(Find(Plain("BETA"), "beta").size() == 1);
	CHECK(Find(Plain("BeTa"), "bEtA").size() == 1);

	Core::SearchQuery exactLowercase = Plain("beta");
	exactLowercase.MatchCase = true;

	CHECK(Find(exactLowercase, "BETA").empty());
	CHECK(Find(exactLowercase, "beta").size() == 1);

	Core::SearchQuery exactUppercase = Plain("BETA");
	exactUppercase.MatchCase = true;

	CHECK(Find(exactUppercase, "beta").empty());
	CHECK(Find(exactUppercase, "BETA").size() == 1);
}

TEST_CASE("Whole word rejects a match glued to an identifier character")
{
	Core::SearchQuery wholeWord = Plain("count");
	wholeWord.WholeWord = true;

	CHECK(Find(wholeWord, "counter").empty());
	CHECK(Find(wholeWord, "_count").empty());
	CHECK(Find(wholeWord, "count9").empty());

	CHECK(Find(wholeWord, "count").size() == 1);
	CHECK(Find(wholeWord, "count(x)").size() == 1);
	CHECK(Find(wholeWord, "a.count = 1").size() == 1);
}

TEST_CASE("Whole word takes a letter beyond ASCII for part of the word, as a double-click does")
{
	Core::SearchQuery wholeWord = Plain("count");
	wholeWord.WholeWord = true;

	CHECK(Find(wholeWord, "count\xC3\xA9").empty());
	CHECK(Find(wholeWord, "\xC3\xA9" "count").empty());

	CHECK(Find(wholeWord, "\xC3\xA9 count").size() == 1);
}

TEST_CASE("FindInText handles the last line without a trailing newline")
{
	const std::vector<Core::SearchHit> hits = Find(Plain("end"), "a\nend");

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].Line == 1);
	CHECK(hits[0].Text == "end");
}

TEST_CASE("FindInText strips a carriage return from the text it reports")
{
	const std::vector<Core::SearchHit> hits = Find(Plain("a"), "a\r\nb\r\n");

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].Text == "a");
}

TEST_CASE("An empty needle finds nothing")
{
	CHECK(Find(Plain(""), "anything at all").empty());
}

TEST_CASE("An empty mask accepts every name")
{
	CHECK(Core::MaskAccepts("", "a.cpp"));
	CHECK(Core::MaskAccepts("", "README"));
}

TEST_CASE("A mask accepts the names it lists and no others")
{
	CHECK(Core::MaskAccepts("*.cpp;*.h", "main.cpp"));
	CHECK(Core::MaskAccepts("*.cpp;*.h", "main.h"));
	CHECK_FALSE(Core::MaskAccepts("*.cpp;*.h", "main.hpp"));
	CHECK_FALSE(Core::MaskAccepts("*.cpp;*.h", "README.md"));

	CHECK(Core::MaskAccepts("premake5.lua", "premake5.lua"));
	CHECK(Core::MaskAccepts("a?c.txt", "abc.txt"));
	CHECK_FALSE(Core::MaskAccepts("a?c.txt", "ac.txt"));
}

TEST_CASE("A mask compares the name it is given and not a path containing the text")
{
	CHECK_FALSE(Core::MaskAccepts("*.h", "h"));
	CHECK(Core::MaskAccepts("*", "anything"));
}

TEST_CASE("A mask ignores case like Windows paths")
{
	CHECK(Core::MaskAccepts("*.CPP", "main.cpp"));
	CHECK(Core::MaskAccepts("*.cpp", "MAIN.CPP"));
}
