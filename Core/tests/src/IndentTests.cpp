#include <doctest/doctest.h>

#include "Core/Text/Indent.h"
#include "Core/Text/TextBuffer.h"

#include <string>
#include <vector>

using namespace core::text;

TEST_CASE("IndentDepth reports the column of the first non-whitespace character")
{
    SUBCASE("no indentation")
    {
        CHECK(IndentDepth("int main() {}", 4) == 0);
    }

    SUBCASE("four spaces")
    {
        CHECK(IndentDepth("    return;", 4) == 4);
    }

    SUBCASE("one tab at tab width four")
    {
        CHECK(IndentDepth("\treturn;", 4) == 4);
    }

    SUBCASE("one tab at tab width eight")
    {
        CHECK(IndentDepth("\treturn;", 8) == 8);
    }

    SUBCASE("a tab following two spaces advances to the tab stop, not by a full width")
    {
        // Two spaces put the column at 2; the tab advances to the next multiple of 4,
        // which is 4 — not 2 + 4 = 6.
        CHECK(IndentDepth("  \tx", 4) == 4);
    }

    SUBCASE("mixed spaces and tabs")
    {
        // Tab from 0 lands at 4; two more spaces land at 6; tab from 6 lands at 8.
        CHECK(IndentDepth("\t  \tx", 4) == 8);
    }

    SUBCASE("a line that is only whitespace has no first non-whitespace character")
    {
        CHECK(IndentDepth("    ", 4) == 0);
        CHECK(IndentDepth("\t\t", 4) == 0);
    }

    SUBCASE("an empty line")
    {
        CHECK(IndentDepth("", 4) == 0);
    }

    SUBCASE("a line whose first character is non-ASCII")
    {
        // "ąb" — no leading whitespace, so depth is 0 whatever the character is.
        const std::string line = "\xC4\x85""b";
        CHECK(IndentDepth(line, 4) == 0);
    }
}

TEST_CASE("IndentDepths resolves blank lines against the lines around them")
{
    // TextBuffer treats a trailing newline as opening a fresh empty line rather than
    // closing the file (see TextBufferTests.cpp), so none of these literals end in
    // '\n' unless a genuine trailing blank line is the point of the case.

    SUBCASE("no blank lines: each line reports its own depth")
    {
        const TextBuffer buffer{"if (x)\n    return;"};

        const std::vector<std::size_t> depths = IndentDepths(buffer, 4);

        REQUIRE(depths.size() == 2);
        CHECK(depths[0] == 0);
        CHECK(depths[1] == 4);
    }

    SUBCASE("a blank line between two lines of equal depth inherits that depth")
    {
        const TextBuffer buffer{"    a();\n\n    b();"};

        const std::vector<std::size_t> depths = IndentDepths(buffer, 4);

        REQUIRE(depths.size() == 3);
        CHECK(depths[0] == 4);
        CHECK(depths[1] == 4);
        CHECK(depths[2] == 4);
    }

    SUBCASE("a blank line between a deep line and a shallow one takes the shallower")
    {
        // Eight-deep above, zero-deep below: the block has closed, so the guide must
        // not hang in the gap at depth 8. The answer is 0, the shallower of the two.
        const TextBuffer buffer{"        return;\n\n}"};

        const std::vector<std::size_t> depths = IndentDepths(buffer, 4);

        REQUIRE(depths.size() == 3);
        CHECK(depths[0] == 8);
        CHECK(depths[1] == 0);
        CHECK(depths[2] == 0);
    }

    SUBCASE("a blank line at the very start of the document has nothing above it")
    {
        // Nothing above counts as depth 0, and the minimum with the deeper line below
        // is still 0 — a guide cannot run above the first non-blank line.
        const TextBuffer buffer{"\n    a();"};

        const std::vector<std::size_t> depths = IndentDepths(buffer, 4);

        REQUIRE(depths.size() == 2);
        CHECK(depths[0] == 0);
        CHECK(depths[1] == 4);
    }

    SUBCASE("a blank line at the very end of the document has nothing below it")
    {
        // Symmetric with the start case: nothing below counts as depth 0, so the
        // trailing blank line is 0 even though the line above it is indented.
        const TextBuffer buffer{"    a();\n\n"};

        const std::vector<std::size_t> depths = IndentDepths(buffer, 4);

        REQUIRE(depths.size() == 3);
        CHECK(depths[0] == 4);
        CHECK(depths[1] == 0);
        CHECK(depths[2] == 0);
    }

    SUBCASE("a document that is entirely blank lines has depth 0 throughout")
    {
        const TextBuffer buffer{"\n\n   "};

        const std::vector<std::size_t> depths = IndentDepths(buffer, 4);

        REQUIRE(depths.size() == 3);
        CHECK(depths[0] == 0);
        CHECK(depths[1] == 0);
        CHECK(depths[2] == 0);
    }

    SUBCASE("a single-line document")
    {
        const TextBuffer buffer{"    x();"};

        const std::vector<std::size_t> depths = IndentDepths(buffer, 4);

        REQUIRE(depths.size() == 1);
        CHECK(depths[0] == 4);
    }
}
