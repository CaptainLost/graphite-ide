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
