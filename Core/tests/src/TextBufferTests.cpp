#include <doctest/doctest.h>

#include "Core/Text/TextBuffer.h"

#include <ostream>
#include <string>

using namespace core::text;

TEST_CASE("A buffer starts out holding what it was given")
{
    const TextBuffer buffer{"hello"};

    CHECK(buffer.Length() == 5);
    CHECK(buffer.Text() == "hello");
    CHECK_FALSE(buffer.Empty());
}

TEST_CASE("An empty buffer is empty")
{
    const TextBuffer buffer;

    CHECK(buffer.Length() == 0);
    CHECK(buffer.Text() == "");
    CHECK(buffer.Empty());
}

TEST_CASE("Text can be inserted at the start, in the middle and at the end")
{
    SUBCASE("at the start")
    {
        TextBuffer buffer{"world"};
        buffer.Insert(0, "hello ");

        CHECK(buffer.Text() == "hello world");
        CHECK(buffer.Length() == 11);
    }

    SUBCASE("in the middle")
    {
        TextBuffer buffer{"held"};
        buffer.Insert(3, "lo wor");

        CHECK(buffer.Text() == "hello word");
    }

    SUBCASE("at the end")
    {
        TextBuffer buffer{"hello"};
        buffer.Insert(5, " world");

        CHECK(buffer.Text() == "hello world");
    }
}

TEST_CASE("Inserting into an empty buffer works")
{
    TextBuffer buffer;
    buffer.Insert(0, "first");

    CHECK(buffer.Text() == "first");
}

TEST_CASE("Inserting nothing changes nothing")
{
    TextBuffer buffer{"hello"};
    buffer.Insert(2, "");

    CHECK(buffer.Text() == "hello");
    CHECK(buffer.Length() == 5);
}

TEST_CASE("Repeated insertion at the same point reads back in order")
{
    // This is what typing looks like: each keystroke inserts one byte after the last.
    TextBuffer buffer;

    for (std::size_t index = 0; index < 6; ++index)
        buffer.Insert(index, std::string(1, static_cast<char>('a' + index)));

    CHECK(buffer.Text() == "abcdef");
}

TEST_CASE("Erasing takes out exactly the range asked for")
{
    SUBCASE("within one piece")
    {
        TextBuffer buffer{"hello world"};
        buffer.Erase(5, 11);

        CHECK(buffer.Text() == "hello");
    }

    SUBCASE("from the start")
    {
        TextBuffer buffer{"hello world"};
        buffer.Erase(0, 6);

        CHECK(buffer.Text() == "world");
    }

    SUBCASE("everything")
    {
        TextBuffer buffer{"hello"};
        buffer.Erase(0, 5);

        CHECK(buffer.Text() == "");
        CHECK(buffer.Empty());
    }
}

TEST_CASE("Erasing across a piece boundary joins what is left on either side")
{
    TextBuffer buffer{"hello"};
    buffer.Insert(5, " world");     // two pieces: "hello" and " world"
    buffer.Erase(3, 8);             // spans the boundary

    CHECK(buffer.Text() == "helrld");
}

TEST_CASE("Erasing whole pieces out of the middle drops them")
{
    TextBuffer buffer{"AZ"};
    buffer.Insert(1, "B");
    buffer.Insert(2, "C");
    buffer.Insert(3, "D");
    REQUIRE(buffer.Text() == "ABCDZ");

    buffer.Erase(1, 4);

    CHECK(buffer.Text() == "AZ");
}

TEST_CASE("Erasing an empty or reversed range changes nothing")
{
    TextBuffer buffer{"hello"};

    buffer.Erase(2, 2);
    CHECK(buffer.Text() == "hello");

    buffer.Erase(4, 1);
    CHECK(buffer.Text() == "hello");
}

TEST_CASE("Erasing past the end stops at the end instead of reading off it")
{
    TextBuffer buffer{"hello"};
    buffer.Erase(3, 99);

    CHECK(buffer.Text() == "hel");
}

TEST_CASE("A substring spans pieces without a seam")
{
    TextBuffer buffer{"hello"};
    buffer.Insert(5, " world");

    CHECK(buffer.Substring(3, 8) == "lo wo");
    CHECK(buffer.Substring(0, buffer.Length()) == "hello world");
}

TEST_CASE("A substring outside the content comes back empty rather than reading off the end")
{
    TextBuffer buffer{"hello"};

    CHECK(buffer.Substring(3, 99) == "lo");
    CHECK(buffer.Substring(99, 120) == "");
    CHECK(buffer.Substring(4, 2) == "");
}

TEST_CASE("Individual bytes can be read across pieces")
{
    TextBuffer buffer{"ab"};
    buffer.Insert(1, "XY");         // aXYb

    CHECK(buffer.CharAt(0) == 'a');
    CHECK(buffer.CharAt(1) == 'X');
    CHECK(buffer.CharAt(2) == 'Y');
    CHECK(buffer.CharAt(3) == 'b');
    CHECK(buffer.CharAt(4) == '\0');
    CHECK(buffer.CharAt(99) == '\0');
}

TEST_CASE("Many edits still read back correctly")
{
    // Enough pieces that a mistake in the piece starts shows up rather than hiding
    // behind a single-piece document.
    TextBuffer buffer;
    std::string expected;

    for (std::size_t index = 0; index < 200; ++index)
    {
        const std::string fragment(1, static_cast<char>('a' + index % 26));
        const std::size_t at = expected.size() / 2;

        buffer.Insert(at, fragment);
        expected.insert(at, fragment);
    }

    CHECK(buffer.Text() == expected);
    CHECK(buffer.Length() == expected.size());
}

TEST_CASE("An empty document is one empty line")
{
    const TextBuffer buffer;

    CHECK(buffer.LineCount() == 1);
    CHECK(buffer.LineText(0) == "");
    CHECK(buffer.LineStart(0) == 0);
    CHECK(buffer.LineLength(0) == 0);
}

TEST_CASE("A trailing newline opens a line rather than closing the file")
{
    const TextBuffer buffer{"a\nb\n"};

    CHECK(buffer.LineCount() == 3);
    CHECK(buffer.LineStart(0) == 0);
    CHECK(buffer.LineStart(1) == 2);
    CHECK(buffer.LineStart(2) == 4);
    CHECK(buffer.LineText(2) == "");
}

TEST_CASE("A file with no trailing newline has no empty line at the end")
{
    const TextBuffer buffer{"a\nb"};

    CHECK(buffer.LineCount() == 2);
    CHECK(buffer.LineText(1) == "b");
}

TEST_CASE("A line's text excludes the newline that ends it")
{
    const TextBuffer buffer{"first\nsecond\nthird"};

    CHECK(buffer.LineText(0) == "first");
    CHECK(buffer.LineText(1) == "second");
    CHECK(buffer.LineText(2) == "third");
    CHECK(buffer.LineLength(1) == 6);
}

TEST_CASE("Consecutive newlines make empty lines")
{
    const TextBuffer buffer{"\n\n\n"};

    CHECK(buffer.LineCount() == 4);

    for (std::size_t line = 0; line < 4; ++line)
        CHECK(buffer.LineText(line) == "");
}

TEST_CASE("Asking about a line that is not there gives the end rather than reading off it")
{
    const TextBuffer buffer{"a\nb"};

    CHECK(buffer.LineStart(99) == buffer.Length());
    CHECK(buffer.LineLength(99) == 0);
    CHECK(buffer.LineText(99) == "");
}

TEST_CASE("Offsets and positions convert both ways")
{
    const TextBuffer buffer{"first\nsecond\nthird"};

    CHECK(buffer.ToPosition(0) == Position{0, 0});
    CHECK(buffer.ToPosition(3) == Position{0, 3});
    CHECK(buffer.ToPosition(5) == Position{0, 5});     // on the newline itself
    CHECK(buffer.ToPosition(6) == Position{1, 0});     // first byte of line 1
    CHECK(buffer.ToPosition(13) == Position{2, 0});

    CHECK(buffer.ToOffset(Position{0, 0}) == 0);
    CHECK(buffer.ToOffset(Position{1, 0}) == 6);
    CHECK(buffer.ToOffset(Position{2, 4}) == 17);
}

TEST_CASE("A position past the end of a line is pulled back to the line's end")
{
    const TextBuffer buffer{"ab\ncdef"};

    CHECK(buffer.ToOffset(Position{0, 99}) == 2);
    CHECK(buffer.ToOffset(Position{1, 99}) == 7);
    CHECK(buffer.ToOffset(Position{99, 0}) == buffer.Length());
}

TEST_CASE("Every offset round-trips through a position")
{
    const TextBuffer buffer{"first\n\nthird line\nfourth"};

    for (std::size_t offset = 0; offset <= buffer.Length(); ++offset)
        CHECK(buffer.ToOffset(buffer.ToPosition(offset)) == offset);
}

TEST_CASE("The line index survives insertion")
{
    SUBCASE("of plain text within a line")
    {
        TextBuffer buffer{"first\nsecond"};
        buffer.Insert(2, "XX");

        CHECK(buffer.LineIndexMatchesRebuild());
        CHECK(buffer.LineCount() == 2);
        CHECK(buffer.LineText(0) == "fiXXrst");
        CHECK(buffer.LineStart(1) == 8);
    }

    SUBCASE("of a newline, which splits a line in two")
    {
        TextBuffer buffer{"abcd"};
        buffer.Insert(2, "\n");

        CHECK(buffer.LineIndexMatchesRebuild());
        CHECK(buffer.LineCount() == 2);
        CHECK(buffer.LineText(0) == "ab");
        CHECK(buffer.LineText(1) == "cd");
    }

    SUBCASE("of several lines at once, as a paste would")
    {
        TextBuffer buffer{"start\nend"};
        buffer.Insert(6, "one\ntwo\nthree\n");

        CHECK(buffer.LineIndexMatchesRebuild());
        CHECK(buffer.LineCount() == 5);
        CHECK(buffer.LineText(1) == "one");
        CHECK(buffer.LineText(3) == "three");
        CHECK(buffer.LineText(4) == "end");
    }

    SUBCASE("at the very start of a line")
    {
        TextBuffer buffer{"a\nb"};
        buffer.Insert(2, "X");

        CHECK(buffer.LineIndexMatchesRebuild());
        CHECK(buffer.LineText(1) == "Xb");
    }

    SUBCASE("at the very end of the document")
    {
        TextBuffer buffer{"a\nb"};
        buffer.Insert(3, "\nc");

        CHECK(buffer.LineIndexMatchesRebuild());
        CHECK(buffer.LineCount() == 3);
        CHECK(buffer.LineText(2) == "c");
    }
}

TEST_CASE("The line index survives erasure")
{
    SUBCASE("within a line")
    {
        TextBuffer buffer{"first\nsecond"};
        buffer.Erase(1, 3);

        CHECK(buffer.LineIndexMatchesRebuild());
        CHECK(buffer.LineText(0) == "fst");
        CHECK(buffer.LineStart(1) == 4);
    }

    SUBCASE("of a newline, which joins two lines")
    {
        TextBuffer buffer{"ab\ncd"};
        buffer.Erase(2, 3);

        CHECK(buffer.LineIndexMatchesRebuild());
        CHECK(buffer.LineCount() == 1);
        CHECK(buffer.LineText(0) == "abcd");
    }

    SUBCASE("across several lines")
    {
        TextBuffer buffer{"one\ntwo\nthree\nfour"};
        buffer.Erase(2, 12);

        CHECK(buffer.LineIndexMatchesRebuild());
        CHECK(buffer.LineCount() == 2);
        CHECK(buffer.LineText(0) == "one");
        CHECK(buffer.LineText(1) == "four");
    }

    SUBCASE("of everything")
    {
        TextBuffer buffer{"one\ntwo\n"};
        buffer.Erase(0, buffer.Length());

        CHECK(buffer.LineIndexMatchesRebuild());
        CHECK(buffer.LineCount() == 1);
        CHECK(buffer.LineText(0) == "");
    }

    SUBCASE("of a trailing newline")
    {
        TextBuffer buffer{"one\n"};
        buffer.Erase(3, 4);

        CHECK(buffer.LineIndexMatchesRebuild());
        CHECK(buffer.LineCount() == 1);
    }
}

TEST_CASE("An edit far down the document leaves the lines above it alone")
{
    std::string content;

    for (std::size_t line = 0; line < 500; ++line)
        content += "line " + std::to_string(line) + "\n";

    TextBuffer buffer{content};
    const std::size_t before = buffer.LineStart(100);

    buffer.Insert(buffer.LineStart(400), "inserted\n");

    CHECK(buffer.LineIndexMatchesRebuild());
    CHECK(buffer.LineStart(100) == before);
    CHECK(buffer.LineCount() == 502);
}
