#include <doctest/doctest.h>

#include "Core/Text/Columns.h"
#include "Core/Jobs/CancellationToken.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/TextBuffer.h"

#include <ostream>
#include <string>

using namespace Core;

namespace {
	constexpr std::size_t kTabWidth = 4;
}

TEST_CASE("In plain ASCII a column is a byte")
{
	const std::string line = "int main()";

	CHECK(ColumnOfByte(line, 0, kTabWidth) == 0);
	CHECK(ColumnOfByte(line, 4, kTabWidth) == 4);
	CHECK(ColumnOfByte(line, line.size(), kTabWidth) == 10);
	CHECK(ColumnCount(line, kTabWidth) == 10);
}

TEST_CASE("A character outside ASCII occupies one column whatever its byte length")
{
	const std::string twoByteLetterBetweenLetters = "a\xC4\x85"
													"b";

	CHECK(ColumnOfByte(twoByteLetterBetweenLetters, 0, kTabWidth) == 0);
	CHECK(ColumnOfByte(twoByteLetterBetweenLetters, 1, kTabWidth) == 1);
	CHECK(ColumnOfByte(twoByteLetterBetweenLetters, 3, kTabWidth) == 2);
	CHECK(ColumnOfByte(twoByteLetterBetweenLetters, 4, kTabWidth) == 3);
	CHECK(ColumnCount(twoByteLetterBetweenLetters, kTabWidth) == 3);
}

TEST_CASE("A four-byte character is still one column")
{
	const std::string emojiBetweenLetters = "a\xF0\x9F\x99\x82"
											"b";

	CHECK(ColumnOfByte(emojiBetweenLetters, 1, kTabWidth) == 1);
	CHECK(ColumnOfByte(emojiBetweenLetters, 5, kTabWidth) == 2);
	CHECK(ColumnCount(emojiBetweenLetters, kTabWidth) == 3);
}

TEST_CASE("A tab advances to the next multiple of the tab width")
{
	SUBCASE("at the start of a line")
	{
		const std::string line = "\tif";

		CHECK(ColumnOfByte(line, 0, kTabWidth) == 0);
		CHECK(ColumnOfByte(line, 1, kTabWidth) == 4);
		CHECK(ColumnOfByte(line, 2, kTabWidth) == 5);
		CHECK(ColumnCount(line, kTabWidth) == 6);
	}

	SUBCASE("in the middle, from a column that is not a multiple")
	{
		const std::string tabSpanningColumnsTwoAndThree = "ab\tc";

		CHECK(ColumnOfByte(tabSpanningColumnsTwoAndThree, 2, kTabWidth) == 2);
		CHECK(ColumnOfByte(tabSpanningColumnsTwoAndThree, 3, kTabWidth) == 4);
		CHECK(ColumnCount(tabSpanningColumnsTwoAndThree, kTabWidth) == 5);
	}

	SUBCASE("from a column that is already a multiple, so it advances a whole width")
	{
		const std::string line = "abcd\te";

		CHECK(ColumnOfByte(line, 4, kTabWidth) == 4);
		CHECK(ColumnOfByte(line, 5, kTabWidth) == 8);
	}

	SUBCASE("two in a row")
	{
		const std::string line = "\t\tx";

		CHECK(ColumnOfByte(line, 1, kTabWidth) == 4);
		CHECK(ColumnOfByte(line, 2, kTabWidth) == 8);
		CHECK(ColumnCount(line, kTabWidth) == 9);
	}

	SUBCASE("immediately before the end")
	{
		const std::string line = "ab\t";

		CHECK(ColumnCount(line, kTabWidth) == 4);
	}
}

TEST_CASE("The tab width is honoured rather than assumed")
{
	const std::string line = "\tx";
	const std::size_t narrowTabWidth = 2;
	const std::size_t wideTabWidth = 8;

	CHECK(ColumnOfByte(line, 1, narrowTabWidth) == 2);
	CHECK(ColumnOfByte(line, 1, wideTabWidth) == 8);
}

TEST_CASE("An empty line is zero columns wide")
{
	CHECK(ColumnCount("", kTabWidth) == 0);
	CHECK(ColumnOfByte("", 0, kTabWidth) == 0);
	CHECK(ByteOfColumn("", 0, kTabWidth) == 0);
	CHECK(ByteOfColumn("", 7, kTabWidth) == 0);
}

TEST_CASE("A byte past the end of the line gives the column past its last")
{
	const std::string line = "abc";

	CHECK(ColumnOfByte(line, 99, kTabWidth) == 3);
}

TEST_CASE("Columns convert back to bytes")
{
	const std::string twoByteLetterBetweenLetters = "a\xC4\x85"
													"b";

	CHECK(ByteOfColumn(twoByteLetterBetweenLetters, 0, kTabWidth) == 0);
	CHECK(ByteOfColumn(twoByteLetterBetweenLetters, 1, kTabWidth) == 1);
	CHECK(ByteOfColumn(twoByteLetterBetweenLetters, 2, kTabWidth) == 3);
	CHECK(ByteOfColumn(twoByteLetterBetweenLetters, 3, kTabWidth) == 4);
}

TEST_CASE("A column past the end of the line gives the line's length")
{
	const std::string line = "abc";

	CHECK(ByteOfColumn(line, 99, kTabWidth) == 3);
}

TEST_CASE("A column inside a tab's run gives the tab itself because a caret cannot sit inside a character")
{
	const std::string tabThenX = "\tx";

	CHECK(ByteOfColumn(tabThenX, 0, kTabWidth) == 0);
	CHECK(ByteOfColumn(tabThenX, 2, kTabWidth) == 0);
	CHECK(ByteOfColumn(tabThenX, 3, kTabWidth) == 0);
	CHECK(ByteOfColumn(tabThenX, 4, kTabWidth) == 1);
}

TEST_CASE("Every byte boundary round-trips through a column")
{
	const std::string line = "\tif (\xC4\x85 == \xF0\x9F\x99\x82)\tx";
	std::size_t boundary = 0;

	while (boundary <= line.size())
	{
		REQUIRE(ByteOfColumn(line, ColumnOfByte(line, boundary, kTabWidth), kTabWidth) == boundary);
		boundary = boundary < line.size() ? NextCharacter(line, boundary) : boundary + 1;
	}
}

TEST_CASE("The widest line is the scrolling extent")
{
	const TextBuffer buffer{"ab\nabcdef\nabc"};

	CHECK(WidestColumn(buffer, kTabWidth) == 6);
}

TEST_CASE("The widest line in columns is not always the longest in bytes")
{
	const std::string eightBytesInFourColumns = "\xC4\x85\xC4\x85\xC4\x85\xC4\x85";
	const std::string sixBytesInSixColumns = "abcdef";
	const TextBuffer buffer{eightBytesInFourColumns + "\n" + sixBytesInSixColumns};

	CHECK(WidestColumn(buffer, kTabWidth) == 6);
}

TEST_CASE("A tab counts as its full run toward the widest line")
{
	const TextBuffer buffer{"\tx\nabcd"};

	CHECK(WidestColumn(buffer, kTabWidth) == 5);
}

TEST_CASE("An empty document has no width")
{
	const TextBuffer buffer;

	CHECK(WidestColumn(buffer, kTabWidth) == 0);
}

TEST_CASE("Expanding tabs leaves a line without any alone")
{
	CHECK(ExpandTabs("int main()", kTabWidth) == "int main()");
}

TEST_CASE("A tab expands to the spaces that reach its next stop")
{
	CHECK(ExpandTabs("\tx", kTabWidth) == "    x");
	CHECK(ExpandTabs("a\tx", kTabWidth) == "a   x");
	CHECK(ExpandTabs("abc\tx", kTabWidth) == "abc x");
	CHECK(ExpandTabs("abcd\tx", kTabWidth) == "abcd    x");
}

TEST_CASE("Consecutive tabs each reach their own stop")
{
	CHECK(ExpandTabs("\t\tx", kTabWidth) == "        x");
	CHECK(ExpandTabs("a\t\tx", kTabWidth) == "a       x");
}

TEST_CASE("A non-ASCII character keeps its bytes and costs one column so a tab after two of them expands to two spaces")
{
	CHECK(ExpandTabs("\xC4\x85\xC4\x85\tx", kTabWidth) == "\xC4\x85\xC4\x85  x");
}

TEST_CASE("An ASCII byte's column is its index into the expansion so the caret lands on the character drawn there")
{
	const std::string line = "\ta\tbc\td";

	const std::string expanded = ExpandTabs(line, kTabWidth);

	for (std::size_t byteOffset = 0; byteOffset <= line.size(); ++byteOffset)
	{
		const std::size_t column = ColumnOfByte(line, byteOffset, kTabWidth);

		REQUIRE(column <= expanded.size());

		if (byteOffset < line.size() && line[byteOffset] != '\t')
		{
			CHECK(expanded[column] == line[byteOffset]);
		}
	}
}

TEST_CASE("An expanded line occupies exactly as many columns as the original")
{
	const std::string line = "\ta\tbc\t\xC4\x85\td";

	CHECK(ColumnCount(ExpandTabs(line, kTabWidth), kTabWidth) == ColumnCount(line, kTabWidth));
}

TEST_CASE("Expanding an empty line gives an empty line")
{
	CHECK(ExpandTabs("", kTabWidth).empty());
}

TEST_CASE("A tab width of zero advances one column at a time")
{
	const std::size_t zeroTabWidth = 0;

	CHECK(ExpandTabs("a\tb", zeroTabWidth) == "a b");
}

TEST_CASE("A byte's offset in expanded text accounts for tabs but not for character width")
{
	const std::string tabThenAbc = "\tabc";
	const std::string tabSpanningColumnsTwoAndThree = "ab\tc";
	const std::string twoByteLetterThenB = "\xC4\x85"
										   "b";
	const std::string tabThenTwoByteLetterThenB = "\t\xC4\x85"
												  "b";
	const std::size_t pastTheEnd = 99;

	CHECK(ExpandedOffsetOfByte(tabThenAbc, 0, kTabWidth) == 0);
	CHECK(ExpandedOffsetOfByte(tabThenAbc, 1, kTabWidth) == 4);
	CHECK(ExpandedOffsetOfByte(tabThenAbc, 2, kTabWidth) == 5);

	CHECK(ExpandedOffsetOfByte(tabSpanningColumnsTwoAndThree, 3, kTabWidth) == 4);

	CHECK(ExpandedOffsetOfByte(twoByteLetterThenB, 2, kTabWidth) == 2);
	CHECK(ExpandedOffsetOfByte(tabThenTwoByteLetterThenB, 3, kTabWidth) == 6);
	CHECK(ColumnOfByte(twoByteLetterThenB, 2, kTabWidth) == 1);

	CHECK(ExpandedOffsetOfByte("ab", pastTheEnd, kTabWidth) == 2);
	CHECK(ExpandedOffsetOfByte("", 0, kTabWidth) == 0);
}

TEST_CASE("Counting the columns of one giant line can be cancelled partway through it")
{
	const std::string giantLine(3 * 1024 * 1024, 'a');
	constexpr std::size_t kSecondSlice = 2;

	CHECK_FALSE(Core::ColumnCountUnlessCancelled(giantLine, kTabWidth, Core::CancellationToken::CancelledAtCheck(kSecondSlice)).has_value());
}

TEST_CASE("Counting columns in slices steps back from a slice end that falls inside a character")
{
	constexpr std::size_t kSliceBytesOfTheCount = 1024 * 1024;
	const std::string threeByteCharacter = "\xe2\x82\xac";
	const Core::CancellationToken keptGoing;

	for (std::size_t bytesBeforeSliceEnd = 1; bytesBeforeSliceEnd < threeByteCharacter.size(); ++bytesBeforeSliceEnd)
	{
		const std::string line = std::string(kSliceBytesOfTheCount - bytesBeforeSliceEnd, 'a') + threeByteCharacter + "\tx\xc4\x85 y";

		CHECK(Core::ColumnCountUnlessCancelled(line, kTabWidth, keptGoing) == Core::ColumnCount(line, kTabWidth));
	}
}

TEST_CASE("Counting columns in slices gives a long run of stray continuation bytes the one column an ordinary count gives it")
{
	const std::string line = "x" + std::string(3 * 1024 * 1024, '\x80') + "\ty";
	const Core::CancellationToken keptGoing;

	CHECK(Core::ColumnCountUnlessCancelled(line, kTabWidth, keptGoing) == Core::ColumnCount(line, kTabWidth));
}