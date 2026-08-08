#include <doctest/doctest.h>

#include "Core/Text/Columns.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/TextBuffer.h"

#include <ostream>
#include <string>

using namespace core::text;

TEST_CASE("In plain ASCII a column is a byte")
{
    const std::string line = "int main()";

    CHECK(ColumnOfByte(line, 0, 4) == 0);
    CHECK(ColumnOfByte(line, 4, 4) == 4);
    CHECK(ColumnOfByte(line, line.size(), 4) == 10);
    CHECK(ColumnCount(line, 4) == 10);
}

TEST_CASE("A character outside ASCII occupies one column whatever its byte length")
{
    // "aąb" — 'ą' is two bytes, so the byte offsets are 0, 1, 3 and 4.
    const std::string line = "a\xC4\x85""b";

    CHECK(ColumnOfByte(line, 0, 4) == 0);
    CHECK(ColumnOfByte(line, 1, 4) == 1);
    CHECK(ColumnOfByte(line, 3, 4) == 2);
    CHECK(ColumnOfByte(line, 4, 4) == 3);
    CHECK(ColumnCount(line, 4) == 3);
}

TEST_CASE("A four-byte character is still one column")
{
    const std::string line = "a\xF0\x9F\x99\x82""b";   // a 🙂 b

    CHECK(ColumnOfByte(line, 1, 4) == 1);
    CHECK(ColumnOfByte(line, 5, 4) == 2);
    CHECK(ColumnCount(line, 4) == 3);
}

TEST_CASE("A tab advances to the next multiple of the tab width")
{
    SUBCASE("at the start of a line")
    {
        const std::string line = "\tif";

        CHECK(ColumnOfByte(line, 0, 4) == 0);
        CHECK(ColumnOfByte(line, 1, 4) == 4);
        CHECK(ColumnOfByte(line, 2, 4) == 5);
        CHECK(ColumnCount(line, 4) == 6);
    }

    SUBCASE("in the middle, from a column that is not a multiple")
    {
        const std::string line = "ab\tc";   // a=0 b=1 tab spans 2..3 c=4

        CHECK(ColumnOfByte(line, 2, 4) == 2);
        CHECK(ColumnOfByte(line, 3, 4) == 4);
        CHECK(ColumnCount(line, 4) == 5);
    }

    SUBCASE("from a column that is already a multiple, so it advances a whole width")
    {
        const std::string line = "abcd\te";

        CHECK(ColumnOfByte(line, 4, 4) == 4);
        CHECK(ColumnOfByte(line, 5, 4) == 8);
    }

    SUBCASE("two in a row")
    {
        const std::string line = "\t\tx";

        CHECK(ColumnOfByte(line, 1, 4) == 4);
        CHECK(ColumnOfByte(line, 2, 4) == 8);
        CHECK(ColumnCount(line, 4) == 9);
    }

    SUBCASE("immediately before the end")
    {
        const std::string line = "ab\t";

        CHECK(ColumnCount(line, 4) == 4);
    }
}

TEST_CASE("The tab width is honoured rather than assumed")
{
    const std::string line = "\tx";

    CHECK(ColumnOfByte(line, 1, 2) == 2);
    CHECK(ColumnOfByte(line, 1, 8) == 8);
}

TEST_CASE("An empty line is zero columns wide")
{
    CHECK(ColumnCount("", 4) == 0);
    CHECK(ColumnOfByte("", 0, 4) == 0);
    CHECK(ByteOfColumn("", 0, 4) == 0);
    CHECK(ByteOfColumn("", 7, 4) == 0);
}

TEST_CASE("A byte past the end of the line gives the column past its last")
{
    const std::string line = "abc";

    CHECK(ColumnOfByte(line, 99, 4) == 3);
}

TEST_CASE("Columns convert back to bytes")
{
    const std::string line = "a\xC4\x85""b";   // aąb

    CHECK(ByteOfColumn(line, 0, 4) == 0);
    CHECK(ByteOfColumn(line, 1, 4) == 1);
    CHECK(ByteOfColumn(line, 2, 4) == 3);
    CHECK(ByteOfColumn(line, 3, 4) == 4);
}

TEST_CASE("A column past the end of the line gives the line's length")
{
    const std::string line = "abc";

    CHECK(ByteOfColumn(line, 99, 4) == 3);
}

TEST_CASE("A column inside a tab's run gives the tab itself")
{
    // "\tx": the tab covers columns 0 to 3, x sits at 4. Clicking anywhere inside the
    // tab has to put the caret before it, not inside a character that has no inside.
    const std::string line = "\tx";

    CHECK(ByteOfColumn(line, 0, 4) == 0);
    CHECK(ByteOfColumn(line, 2, 4) == 0);
    CHECK(ByteOfColumn(line, 3, 4) == 0);
    CHECK(ByteOfColumn(line, 4, 4) == 1);
}

TEST_CASE("Every byte boundary round-trips through a column")
{
    const std::string line = "\tif (\xC4\x85 == \xF0\x9F\x99\x82)\tx";
    std::size_t byte = 0;

    while (byte <= line.size())
    {
        REQUIRE(ByteOfColumn(line, ColumnOfByte(line, byte, 4), 4) == byte);
        byte = byte < line.size() ? core::text::NextCharacter(line, byte) : byte + 1;
    }
}

TEST_CASE("The widest line is the scrolling extent")
{
    const TextBuffer buffer{"ab\nabcdef\nabc"};

    CHECK(WidestColumn(buffer, 4) == 6);
}

TEST_CASE("The widest line in columns is not always the longest in bytes")
{
    // The first line is 8 bytes and 4 columns; the second is 6 bytes and 6 columns.
    const TextBuffer buffer{"\xC4\x85\xC4\x85\xC4\x85\xC4\x85\nabcdef"};

    CHECK(WidestColumn(buffer, 4) == 6);
}

TEST_CASE("A tab counts as its full run toward the widest line")
{
    const TextBuffer buffer{"\tx\nabcd"};

    CHECK(WidestColumn(buffer, 4) == 5);
}

TEST_CASE("An empty document has no width")
{
    const TextBuffer buffer;

    CHECK(WidestColumn(buffer, 4) == 0);
}
