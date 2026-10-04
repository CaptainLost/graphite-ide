#include <doctest/doctest.h>

#include "Core/Text/WordBounds.h"

using namespace Core;

TEST_CASE("A word in code is letters, digits and underscores")
{
	CHECK(WordSpanAt("auto my_value2 = 3;", 7, WordKind::Code) == ColumnSpan{5, 14});
}

TEST_CASE("A byte on punctuation selects that one character")
{
	CHECK(WordSpanAt("a = b;", 2, WordKind::Code) == ColumnSpan{2, 3});
}

TEST_CASE("A byte at the start or end of a word still selects the word")
{
	CHECK(WordSpanAt("foo bar", 4, WordKind::Code) == ColumnSpan{4, 7});
	CHECK(WordSpanAt("foo bar", 2, WordKind::Code) == ColumnSpan{0, 3});
}

TEST_CASE("Polish letters belong to the word and are never split")
{
	// "zażółć gęślą": "zażółć" is bytes 0..10.
	CHECK(WordSpanAt("za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 g", 3, WordKind::Code) == ColumnSpan{0, 10});
}

TEST_CASE("A path word spans separators, dots, dashes and a line suffix")
{
	CHECK(WordSpanAt("error in src/main-app.cpp:42 here", 12, WordKind::Path) == ColumnSpan{9, 28});
	CHECK(WordSpanAt("error in src/main-app.cpp:42 here", 11, WordKind::Code) == ColumnSpan{9, 12});
	// Byte 12 is the '/' itself, which is punctuation in code.
	CHECK(WordSpanAt("error in src/main-app.cpp:42 here", 12, WordKind::Code) == ColumnSpan{12, 13});
}

TEST_CASE("Past the end of the line selects nothing")
{
	CHECK(WordSpanAt("abc", 3, WordKind::Code) == ColumnSpan{3, 3});
	CHECK(WordSpanAt("", 0, WordKind::Code) == ColumnSpan{0, 0});
}
