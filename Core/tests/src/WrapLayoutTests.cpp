#include <doctest/doctest.h>

#include "Core/Text/WrapLayout.h"

#include <string>
#include <vector>

using namespace core::text;

TEST_CASE("A line that fits does not break")
{
    CHECK(WrapColumns("", 10, 4).empty());
    CHECK(WrapColumns("short", 10, 4).empty());
}

TEST_CASE("A line exactly as wide as the row does not break")
{
    // Ten columns in a ten column row: the last character lands on the last cell,
    // and a break after it would make an empty trailing row.
    CHECK(WrapColumns("0123456789", 10, 4).empty());
}

TEST_CASE("A break goes after the last space that fits")
{
    // Columns: "alpha beta gamma", limit 12. The space at column 10 is the last
    // one at or before the limit, so the second row starts at column 11.
    const std::vector<std::size_t> breaks = WrapColumns("alpha beta gamma", 12, 4);

    REQUIRE(breaks.size() == 1);
    CHECK(breaks[0] == 11);
}

TEST_CASE("A word longer than the row breaks hard at the limit")
{
    const std::vector<std::size_t> breaks = WrapColumns("aaaaaaaaaaaaaaa", 5, 4);

    REQUIRE(breaks.size() == 2);
    CHECK(breaks[0] == 5);
    CHECK(breaks[1] == 10);
}

TEST_CASE("Runs of spaces stay on the row they end")
{
    // "aa    bb" with a limit of 6: breaking after the run leaves the next row
    // starting at 'b', never at a space.
    const std::vector<std::size_t> breaks = WrapColumns("aa    bb", 6, 4);

    REQUIRE(breaks.size() == 1);
    CHECK(breaks[0] == 6);
}

TEST_CASE("A tab counts as the columns it advances, not as one character")
{
    // "\tabcdef" with tabWidth 4 occupies 10 columns: 4 for the tab, 6 for the
    // letters. With a limit of 6 the break falls inside the letters, at column 6.
    const std::vector<std::size_t> breaks = WrapColumns("\tabcdef", 6, 4);

    REQUIRE(breaks.size() == 1);
    CHECK(breaks[0] == 6);
}

TEST_CASE("A character outside ASCII is one column whatever its byte length")
{
    // "ąąąą" is four characters and eight bytes. With a limit of 2 it breaks
    // twice, on column boundaries, never mid-character.
    const std::vector<std::size_t> breaks = WrapColumns("\xC4\x85\xC4\x85\xC4\x85\xC4\x85", 2, 4);

    REQUIRE(breaks.size() == 1);
    CHECK(breaks[0] == 2);
}

TEST_CASE("A degenerate row width still terminates")
{
    // A window dragged narrower than one cell is a layout state, not an error.
    CHECK(WrapColumns("abc", 1, 4).size() == 2);
    CHECK(WrapColumns("abc", 0, 4).size() == 2);
}

TEST_CASE("A space run reaching past the limit keeps the whole run on its row")
{
    // The run is invisible, so letting it overflow the row is right; starting the next
    // row with a space is not.
    const std::vector<std::size_t> breaks = WrapColumns("aa      bb", 4, 4);

    REQUIRE(breaks.size() == 1);
    CHECK(breaks[0] == 8);
}

TEST_CASE("A line of nothing but spaces never breaks")
{
    CHECK(WrapColumns("      ", 3, 4).empty());
}

TEST_CASE("A hard break never lands inside a tab")
{
    // Columns: "aaaa" is 0-3, the tab spans 4-7, "bbbb" is 8-11. Breaking at the limit of
    // 6 would split the tab, so the break moves back to where the tab starts.
    const std::vector<std::size_t> breaks = WrapColumns("aaaa\tbbbb", 6, 4);

    REQUIRE(!breaks.empty());
    CHECK(breaks[0] == 4);
}

TEST_CASE("A tab wider than the row still advances")
{
    // The tab starts at the row's own start, so it cannot move back any further; the
    // break goes after it instead of standing still.
    const std::vector<std::size_t> breaks = WrapColumns("\tabc", 2, 4);

    REQUIRE(!breaks.empty());
    CHECK(breaks[0] == 4);
}
