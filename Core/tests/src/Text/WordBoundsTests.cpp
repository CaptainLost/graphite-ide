#include <doctest/doctest.h>

#include "Core/Text/WordBounds.h"

#include <string_view>

using namespace Core;

TEST_CASE("A word in code is letters, digits and underscores")
{
	const std::size_t underscoreInsideMyValue2 = 7;

	CHECK(WordSpanAt("auto my_value2 = 3;", underscoreInsideMyValue2, WordKind::Code) == ColumnSpan{.From = 5, .To = 14});
}

TEST_CASE("A byte belongs to a word when its character does in code, every byte of a letter beyond ASCII included")
{
	CHECK(IsWordByte('a'));
	CHECK(IsWordByte('Z'));
	CHECK(IsWordByte('7'));
	CHECK(IsWordByte('_'));
	CHECK(IsWordByte('\xC3'));
	CHECK(IsWordByte('\xA9'));

	CHECK_FALSE(IsWordByte(' '));
	CHECK_FALSE(IsWordByte('.'));
	CHECK_FALSE(IsWordByte('('));
	CHECK_FALSE(IsWordByte('-'));
}

TEST_CASE("A byte on punctuation selects that one character")
{
	const std::size_t equalsSign = 2;

	CHECK(WordSpanAt("a = b;", equalsSign, WordKind::Code) == ColumnSpan{.From = 2, .To = 3});
}

TEST_CASE("A byte at the start or end of a word still selects the word")
{
	const std::size_t firstByteOfBar = 4;
	const std::size_t lastByteOfFoo = 2;

	CHECK(WordSpanAt("foo bar", firstByteOfBar, WordKind::Code) == ColumnSpan{.From = 4, .To = 7});
	CHECK(WordSpanAt("foo bar", lastByteOfFoo, WordKind::Code) == ColumnSpan{.From = 0, .To = 3});
}

TEST_CASE("Polish letters belong to the word and are never split")
{
	const std::size_t secondByteOfZWithDot = 3;
	const ColumnSpan allTenBytesOfFirstWord{.From = 0, .To = 10};

	CHECK(WordSpanAt("zażółć g", secondByteOfZWithDot, WordKind::Code) == allTenBytesOfFirstWord);
}

TEST_CASE("A path word spans separators, dots, dashes and a line suffix")
{
	const std::string_view lineText = "error in src/main-app.cpp:42 here";
	const std::size_t slash = 12;
	const std::size_t lastByteOfSrc = 11;
	const ColumnSpan slashAloneAsPunctuationInCode{.From = 12, .To = 13};

	CHECK(WordSpanAt(lineText, slash, WordKind::Path) == ColumnSpan{.From = 9, .To = 28});
	CHECK(WordSpanAt(lineText, lastByteOfSrc, WordKind::Code) == ColumnSpan{.From = 9, .To = 12});
	CHECK(WordSpanAt(lineText, slash, WordKind::Code) == slashAloneAsPunctuationInCode);
}

TEST_CASE("Past the end of the line selects nothing as an empty span at the line's end")
{
	const std::size_t pastEndOfAbc = 3;
	const std::size_t endOfEmptyLine = 0;

	CHECK(WordSpanAt("abc", pastEndOfAbc, WordKind::Code) == ColumnSpan{.From = 3, .To = 3});
	CHECK(WordSpanAt("", endOfEmptyLine, WordKind::Code) == ColumnSpan{.From = 0, .To = 0});
}
