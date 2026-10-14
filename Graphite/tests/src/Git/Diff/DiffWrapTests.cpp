#include <doctest/doctest.h>

#include "Git/Diff/DiffWrap.h"

#include <cstddef>
#include <string_view>
#include <vector>

using Graphite::DiffRowLines;
using Graphite::SegmentOf;
using Graphite::WrapBytes;
using Graphite::WrapSegment;

using Breaks = std::vector<std::size_t>;

TEST_CASE("A diff line that fits its columns is not wrapped")
{
	CHECK(WrapBytes("", 10).empty());
	CHECK(WrapBytes("abc def", 10).empty());
	CHECK(WrapBytes("0123456789", 10).empty());
}

TEST_CASE("A diff line wraps after the last space before the edge")
{
	CHECK(WrapBytes("alpha beta gamma", 12) == Breaks{11});
	CHECK(WrapBytes("one two three four", 8) == Breaks{8, 14});
}

TEST_CASE("A run of spaces crossing the edge stays on the row it ends")
{
	CHECK(WrapBytes("abcd      efgh", 6) == Breaks{10});
}

TEST_CASE("Spaces running to the end of a diff line add no row")
{
	CHECK(WrapBytes("abcd        ", 6).empty());
}

TEST_CASE("A word wider than the row is broken hard at the edge")
{
	CHECK(WrapBytes("abcdefghij", 4) == Breaks{4, 8});
}

TEST_CASE("A tab in a diff line takes four cells wherever it falls, so a tab after one letter makes six")
{
	CHECK(WrapBytes("a\tb", 6).empty());
	CHECK(WrapBytes("a\tb", 5) == Breaks{2});
}

TEST_CASE("A diff line is never broken inside a multi-byte character")
{
	constexpr std::string_view threeLettersOfTwoBytesAndOneCellEach =
		"\xC5\xBC\xC5\xBC\xC5\xBC";

	CHECK(WrapBytes(threeLettersOfTwoBytesAndOneCellEach, 2) == Breaks{4});
}

TEST_CASE("Zero columns wraps one character to a row rather than standing still")
{
	CHECK(WrapBytes("abc", 0) == Breaks{1, 2});
}

TEST_CASE("A wrapped diff line's segments cover it end to end")
{
	constexpr std::string_view text = "alpha beta gamma";
	const Breaks breaks = WrapBytes(text, 12);

	const WrapSegment first = SegmentOf(text, breaks, 0);
	const WrapSegment second = SegmentOf(text, breaks, 1);

	CHECK(first.From == 0);
	CHECK(first.To == 11);
	CHECK(second.From == 11);
	CHECK(second.To == text.size());
}

TEST_CASE("Past the last segment a diff line has nothing left")
{
	const WrapSegment past = SegmentOf("abc", Breaks{}, 3);

	CHECK(past.From == 3);
	CHECK(past.To == 3);
}

TEST_CASE("Rows that do not wrap take one line each")
{
	const DiffRowLines lines = DiffRowLines::Uniform(5);

	CHECK(lines.RowCount() == 5);
	CHECK(lines.TotalLines() == 5);
	CHECK(lines.FirstLineOf(3) == 3);
	CHECK(lines.LinesOf(3) == 1);
	CHECK(lines.RowOfLine(3) == 3);
	CHECK(lines.RowOfLine(40) == 4);
}

TEST_CASE("A wrapped row pushes every row after it down")
{
	const std::vector<std::size_t> counts{1, 3, 1, 2};
	const DiffRowLines lines = DiffRowLines::FromCounts(counts);

	CHECK(lines.TotalLines() == 7);
	CHECK(lines.FirstLineOf(0) == 0);
	CHECK(lines.FirstLineOf(1) == 1);
	CHECK(lines.FirstLineOf(2) == 4);
	CHECK(lines.FirstLineOf(3) == 5);
	CHECK(lines.LinesOf(1) == 3);

	CHECK(lines.RowOfLine(0) == 0);
	CHECK(lines.RowOfLine(1) == 1);
	CHECK(lines.RowOfLine(3) == 1);
	CHECK(lines.RowOfLine(4) == 2);
	CHECK(lines.RowOfLine(6) == 3);
	CHECK(lines.RowOfLine(100) == 3);
}

TEST_CASE("A row counted as taking no lines still takes one")
{
	const std::vector<std::size_t> counts{0, 2};
	const DiffRowLines lines = DiffRowLines::FromCounts(counts);

	CHECK(lines.TotalLines() == 3);
	CHECK(lines.FirstLineOf(1) == 1);
}

TEST_CASE("No rows take no lines")
{
	const DiffRowLines lines;

	CHECK(lines.RowCount() == 0);
	CHECK(lines.TotalLines() == 0);
	CHECK(lines.RowOfLine(5) == 0);
}
