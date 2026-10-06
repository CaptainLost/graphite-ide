#include <doctest/doctest.h>

#include "Core/Text/TextSelection.h"

#include <string>
#include <vector>

using namespace Core;

TEST_CASE("A press that has not moved selects nothing")
{
	TextSelection selection;
	selection.Begin({.Line = 3, .Column = 4});
	CHECK(selection.IsEmpty());
	CHECK_FALSE(selection.SpanOn(3, 10).has_value());
}

TEST_CASE("A selection orders its ends whichever way it was dragged")
{
	TextSelection selection;
	selection.Begin({.Line = 5, .Column = 2});
	selection.ExtendTo({.Line = 3, .Column = 7});
	CHECK(selection.First() == TextPoint{.Line = 3, .Column = 7});
	CHECK(selection.Last() == TextPoint{.Line = 5, .Column = 2});
	CHECK(selection.Pressed() == TextPoint{.Line = 5, .Column = 2});
}

TEST_CASE("A selection spans the tail of its first line, all of the middle and the head of its last")
{
	TextSelection selection;
	selection.Begin({.Line = 1, .Column = 3});
	selection.ExtendTo({.Line = 3, .Column = 2});
	CHECK(selection.SpanOn(1, 10) == ColumnSpan{.From = 3, .To = 10});
	CHECK(selection.SpanOn(2, 6) == ColumnSpan{.From = 0, .To = 6});
	CHECK(selection.SpanOn(3, 10) == ColumnSpan{.From = 0, .To = 2});
	CHECK_FALSE(selection.SpanOn(0, 10).has_value());
	CHECK_FALSE(selection.SpanOn(4, 10).has_value());
}

TEST_CASE("A span is clamped to the line's length")
{
	TextSelection selection;
	selection.Begin({.Line = 1, .Column = 8});
	selection.ExtendTo({.Line = 1, .Column = 20});
	CHECK(selection.SpanOn(1, 5) == ColumnSpan{.From = 5, .To = 5});
}

TEST_CASE("Extending keeps the pressed end, which is what Shift+click stretches from")
{
	TextSelection selection;
	selection.Select({.Line = 2, .Column = 0}, {.Line = 2, .Column = 4});
	selection.ExtendTo({.Line = 4, .Column = 1});
	CHECK(selection.First() == TextPoint{.Line = 2, .Column = 0});
	CHECK(selection.Last() == TextPoint{.Line = 4, .Column = 1});
	CHECK(selection.Pressed() == TextPoint{.Line = 2, .Column = 0});
}

TEST_CASE("Clear empties the selection")
{
	TextSelection selection;
	selection.Select({.Line = 0, .Column = 0}, {.Line = 1, .Column = 1});
	selection.Clear();
	CHECK(selection.IsEmpty());
}

TEST_CASE("JoinSelected takes each line's selected part one per output line and nothing from lines the selection does not reach")
{
	TextSelection selection;
	selection.Begin({.Line = 10, .Column = 4});
	selection.ExtendTo({.Line = 12, .Column = 3});
	const std::vector<SelectableLine> lines{{.Line = 10, .Text = "int main()"}, {.Line = 11, .Text = "{"}, {.Line = 12, .Text = "}  // end"}};
	CHECK(JoinSelected(selection, lines) == "main()\n{\n}  ");

	const std::vector<SelectableLine> linesAroundTheSelection{{.Line = 9, .Text = "before"}, {.Line = 10, .Text = "int main()"}, {.Line = 13, .Text = "after"}};
	CHECK(JoinSelected(selection, linesAroundTheSelection) == "main()");
}

TEST_CASE("JoinSelected skips lines the caller does not show")
{
	TextSelection selection;
	selection.Begin({.Line = 1, .Column = 0});
	selection.ExtendTo({.Line = 3, .Column = 1});
	const std::vector<SelectableLine> lines{{.Line = 1, .Text = "a"}, {.Line = 3, .Text = "c"}};
	CHECK(JoinSelected(selection, lines) == "a\nc");
}

TEST_CASE("JoinSelected never splits a UTF-8 character and moves a byte column inside one back to its first byte")
{
	constexpr std::size_t kSecondByteOfSecondCharacter = 3;
	constexpr std::size_t kEndOfWord = 7;

	TextSelection selection;
	selection.Begin({.Line = 1, .Column = kSecondByteOfSecondCharacter});
	selection.ExtendTo({.Line = 1, .Column = kEndOfWord});
	const std::vector<SelectableLine> lines{{.Line = 1, .Text = "\xC5\xBC\xC3\xB3\xC5\x82w"}};
	CHECK(JoinSelected(selection, lines) == "\xC3\xB3\xC5\x82w");
}

TEST_CASE("JoinSelected of an empty selection is empty")
{
	TextSelection selection;
	const std::vector<SelectableLine> lines{{.Line = 0, .Text = "text"}};
	CHECK(JoinSelected(selection, lines).empty());
}
