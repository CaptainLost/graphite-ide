#include <doctest/doctest.h>

#include "Sessions/Requests/RequestTimeline.h"
#include "Sessions/Requests/SentTextLayout.h"
#include "Sessions/Transcript/TranscriptReader.h"

#include "TranscriptFixtures.h"

#include <string>
#include <utility>
#include <vector>

using namespace Graphite;

namespace {
	using Lines = std::vector<std::pair<std::size_t, std::size_t>>;
}

TEST_CASE("A line that fits is one line, and an empty text has none")
{
	CHECK(WrapLines("hello", 80) == Lines{{0, 5}});
	CHECK(WrapLines("", 80).empty());
}

TEST_CASE("A long line breaks after the last space that fits")
{
	CHECK(WrapLines("aaa bbb ccc", 7) == Lines{{0, 4}, {4, 11}});
}

TEST_CASE("A line with no space breaks where the width runs out")
{
	CHECK(WrapLines("abcdefgh", 3) == Lines{{0, 3}, {3, 6}, {6, 8}});
}

TEST_CASE("Width is counted in characters and a break never splits one")
{
	// "zażółć" is six characters in ten bytes; "gęślą" five in eight.
	const std::string text = "za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 g\xc4\x99\xc5\x9bl\xc4\x85";
	REQUIRE(text.size() == 19);

	// The space the break fell on starts no row.
	CHECK(WrapLines(text, 6) == Lines{{0, 10}, {11, 19}});
}

TEST_CASE("A row broken right before a space does not start the next row with it")
{
	CHECK(WrapLines("abc def", 3) == Lines{{0, 3}, {4, 7}});

	// A line's own leading space is text, not a break's leftover.
	CHECK(WrapLines(" abc", 80) == Lines{{0, 4}});
	CHECK(WrapLines("ab\n cd", 80) == Lines{{0, 2}, {3, 6}});

	// A space the width ran out on at the very end leaves no empty row behind.
	CHECK(WrapLines("abc ", 3) == Lines{{0, 3}});
}

TEST_CASE("Line ends split lines, a carriage return before one is dropped, and a final one adds no empty line")
{
	CHECK(WrapLines("ab\r\ncd", 80) == Lines{{0, 2}, {4, 6}});
	CHECK(WrapLines("ab\n", 80) == Lines{{0, 2}});
	CHECK(WrapLines("a\n\nb", 80) == Lines{{0, 1}, {2, 2}, {3, 4}});
	CHECK(WrapLines("\n", 80) == Lines{{0, 0}});
}

TEST_CASE("A tab takes several columns")
{
	CHECK(WrapLines("\tabc", 5) == Lines{{0, 2}, {2, 4}});
}

TEST_CASE("A width of zero is taken as one column rather than looping")
{
	CHECK(WrapLines("ab", 0) == Lines{{0, 1}, {1, 2}});
}

TEST_CASE("The layout gives each segment a header, then its text's rows")
{
	SentTextLayout layout;
	layout.Build({"one", "", "two\nthree"}, 80);

	const std::vector<SentTextRow>& rows = layout.GetRows();
	REQUIRE(rows.size() == 6);
	CHECK(rows[0].Header);
	CHECK(rows[0].Segment == 0);
	CHECK_FALSE(rows[1].Header);
	CHECK(rows[1].Segment == 0);
	CHECK(rows[1].Begin == 0);
	CHECK(rows[1].End == 3);
	CHECK(rows[2].Header);
	CHECK(rows[2].Segment == 1);
	CHECK(rows[3].Header);
	CHECK(rows[3].Segment == 2);
	CHECK(rows[4].End == 3);
	CHECK(rows[5].Begin == 4);
	CHECK(rows[5].End == 9);

	CHECK(layout.FirstRowOf(0) == 0);
	CHECK(layout.FirstRowOf(1) == 2);
	CHECK(layout.FirstRowOf(2) == 3);
	CHECK(layout.FirstRowOf(3) == rows.size());
}

TEST_CASE("A request's segments are what it sent, then what it returned")
{
	const std::vector<ApiRequest> requests = BuildRequestTimeline(
		ParseTranscript(Fixtures::UserPrompt("go") +
						Fixtures::Assistant("r1", Fixtures::TextBlock("done"), Fixtures::Usage(1, 1)))
			.Records);

	REQUIRE(requests.size() == 1);
	CHECK(SegmentCount(requests[0]) == 2);
	CHECK(SegmentText(requests[0], 0) == "go");
	CHECK(SegmentText(requests[0], 1) == "done");
	CHECK(SegmentText(requests[0], 2).empty());
	CHECK(AllSegmentText(requests[0]) == "go\n\ndone");
}

TEST_CASE("DisplayText notes an empty thinking block instead of showing nothing, but SegmentText stays empty")
{
	const std::vector<ApiRequest> requests = BuildRequestTimeline(
		ParseTranscript(Fixtures::UserPrompt("go") +
						Fixtures::Assistant("r1", Fixtures::ThinkingBlock(""), Fixtures::Usage(1, 1)) +
						Fixtures::Assistant("r2", Fixtures::ThinkingBlock("hmm"), Fixtures::Usage(1, 1)))
			.Records);

	REQUIRE(requests.size() == 2);
	REQUIRE(requests[0].Response.size() == 1);
	REQUIRE(requests[1].Response.size() == 1);

	const std::size_t emptySegment = requests[0].Sent.size();
	const std::size_t fullSegment = requests[1].Sent.size();

	CHECK(SegmentText(requests[0], emptySegment).empty());
	CHECK(DisplayText(requests[0], emptySegment) ==
		  "The model reasoned here, but the API returns reasoning hidden: only an encrypted signature reaches "
		  "Claude Code, so there is no text to show. Its tokens are part of this reply's output.");

	CHECK(SegmentText(requests[1], fullSegment) == "hmm");
	CHECK(DisplayText(requests[1], fullSegment) == "hmm");
}
