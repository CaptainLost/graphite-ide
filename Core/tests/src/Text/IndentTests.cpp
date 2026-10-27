#include <doctest/doctest.h>

#include "Core/Text/DocumentSet.h"
#include "Core/Text/Indent.h"
#include "Core/Text/TextBuffer.h"

#include <string>
#include <vector>

using namespace Core;

namespace {
	constexpr std::size_t kTabWidth = 4;
	constexpr std::size_t kWideTabWidth = 8;
}

TEST_CASE("IndentDepth reports the column of the first non-whitespace character")
{
	SUBCASE("no indentation")
	{
		CHECK(IndentDepth("int main() {}", kTabWidth) == 0);
	}

	SUBCASE("four spaces")
	{
		CHECK(IndentDepth("    return;", kTabWidth) == 4);
	}

	SUBCASE("one tab at tab width four")
	{
		CHECK(IndentDepth("\treturn;", kTabWidth) == 4);
	}

	SUBCASE("one tab at tab width eight")
	{
		CHECK(IndentDepth("\treturn;", kWideTabWidth) == 8);
	}

	SUBCASE("a tab following two spaces advances to the tab stop at 4 rather than by a full width to 6")
	{
		CHECK(IndentDepth("  \tx", kTabWidth) == 4);
	}

	SUBCASE("mixed spaces and tabs: a tab to 4 then two spaces to 6 then a tab to 8")
	{
		CHECK(IndentDepth("\t  \tx", kTabWidth) == 8);
	}

	SUBCASE("a line that is only whitespace has no first non-whitespace character")
	{
		CHECK(IndentDepth("    ", kTabWidth) == 0);
		CHECK(IndentDepth("\t\t", kTabWidth) == 0);
	}

	SUBCASE("an empty line")
	{
		CHECK(IndentDepth("", kTabWidth) == 0);
	}

	SUBCASE("a tab followed by a two-byte character reports the four columns of the tab rather than the one byte before the character")
	{
		const std::string tabThenTwoByteCharacter = "\t\xC4\x85";
		CHECK(IndentDepth(tabThenTwoByteCharacter, kTabWidth) == 4);
	}
}

TEST_CASE("IndentDepths resolves blank lines against the lines around them")
{
	// TextBuffer opens a fresh empty line after a trailing newline, so a literal here ends in
	// '\n' only where a trailing blank line is the point of the case.

	SUBCASE("no blank lines: each line reports its own depth")
	{
		const TextBuffer buffer{"if (x)\n    return;"};

		const std::vector<std::size_t> depths = IndentDepths(buffer, kTabWidth);

		REQUIRE(depths.size() == 2);
		CHECK(depths[0] == 0);
		CHECK(depths[1] == 4);
	}

	SUBCASE("a blank line between two lines of equal depth inherits that depth")
	{
		const TextBuffer buffer{"    a();\n\n    b();"};

		const std::vector<std::size_t> depths = IndentDepths(buffer, kTabWidth);

		REQUIRE(depths.size() == 3);
		CHECK(depths[0] == 4);
		CHECK(depths[1] == 4);
		CHECK(depths[2] == 4);
	}

	SUBCASE("a blank line between a deep line and a shallow one takes the shallower so the guide of a closed block does not hang in the gap")
	{
		const TextBuffer buffer{"        return;\n\n}"};

		const std::vector<std::size_t> depths = IndentDepths(buffer, kTabWidth);

		REQUIRE(depths.size() == 3);
		CHECK(depths[0] == 8);
		CHECK(depths[1] == 0);
		CHECK(depths[2] == 0);
	}

	SUBCASE("a blank line at the very start of the document counts the missing line above it as depth 0")
	{
		const TextBuffer buffer{"\n    a();"};

		const std::vector<std::size_t> depths = IndentDepths(buffer, kTabWidth);

		REQUIRE(depths.size() == 2);
		CHECK(depths[0] == 0);
		CHECK(depths[1] == 4);
	}

	SUBCASE("a blank line at the very end of the document counts the missing line below it as depth 0")
	{
		const TextBuffer buffer{"    a();\n\n"};

		const std::vector<std::size_t> depths = IndentDepths(buffer, kTabWidth);

		REQUIRE(depths.size() == 3);
		CHECK(depths[0] == 4);
		CHECK(depths[1] == 0);
		CHECK(depths[2] == 0);
	}

	SUBCASE("a run of consecutive blank lines all resolve to the same shallower depth instead of one that decays across the run")
	{
		const TextBuffer buffer{"        return;\n\n\n\n    x();"};

		const std::vector<std::size_t> depths = IndentDepths(buffer, kTabWidth);

		REQUIRE(depths.size() == 5);
		CHECK(depths[0] == 8);
		CHECK(depths[1] == 4);
		CHECK(depths[2] == 4);
		CHECK(depths[3] == 4);
		CHECK(depths[4] == 4);
	}

	SUBCASE("a document that is entirely blank lines has depth 0 throughout")
	{
		const TextBuffer buffer{"\n\n   "};

		const std::vector<std::size_t> depths = IndentDepths(buffer, kTabWidth);

		REQUIRE(depths.size() == 3);
		CHECK(depths[0] == 0);
		CHECK(depths[1] == 0);
		CHECK(depths[2] == 0);
	}

	SUBCASE("a single-line document")
	{
		const TextBuffer buffer{"    x();"};

		const std::vector<std::size_t> depths = IndentDepths(buffer, kTabWidth);

		REQUIRE(depths.size() == 1);
		CHECK(depths[0] == 4);
	}
}

TEST_CASE("IndentDepths measures a leading tab at the width it is given")
{
	const TextBuffer buffer{"\tone\n\t\ttwo\n"};

	const std::vector<std::size_t> depthsAtTabWidth = IndentDepths(buffer, kTabWidth);
	const std::vector<std::size_t> depthsAtWideTabWidth = IndentDepths(buffer, kWideTabWidth);

	REQUIRE(depthsAtTabWidth.size() >= 2);
	REQUIRE(depthsAtWideTabWidth.size() >= 2);

	CHECK(depthsAtTabWidth[0] == 4);
	CHECK(depthsAtTabWidth[1] == 8);

	CHECK(depthsAtWideTabWidth[0] == 8);
	CHECK(depthsAtWideTabWidth[1] == 16);
}

TEST_CASE("OpenItem defaults its tab width to the shared default")
{
	const OpenItem item;

	CHECK(item.TabWidth == DefaultTabWidth);
}

TEST_CASE("Indent depths cancelled partway through many lines answer nothing")
{
	const Core::TextBuffer buffer{std::string(20000, '\n')};
	constexpr std::size_t kSecondAsk = 2;

	CHECK_FALSE(Core::IndentDepthsUnlessCancelled(buffer, kTabWidth, Core::CancellationToken::CancelledAtCheck(kSecondAsk)).has_value());
}

TEST_CASE("Indent depths cancelled partway through a few long lines answer nothing")
{
	const std::string longLine(700 * 1024, 'a');
	const Core::TextBuffer buffer{longLine + "\n" + longLine + "\n" + longLine};
	constexpr std::size_t kSecondAsk = 2;

	CHECK_FALSE(Core::IndentDepthsUnlessCancelled(buffer, kTabWidth, Core::CancellationToken::CancelledAtCheck(kSecondAsk)).has_value());
}
TEST_CASE("A line indented past the first bytes read for its indent still measures its whole indent")
{
	const std::string deepIndent(300, ' ');
	const Core::TextBuffer buffer{"top\n" + deepIndent + "deep\n" + deepIndent};

	const std::vector<std::size_t> depths = Core::IndentDepths(buffer, kTabWidth);

	REQUIRE(depths.size() == 3);
	CHECK(depths[0] == 0);
	CHECK(depths[1] == 300);
	CHECK(depths[2] == 0);
}