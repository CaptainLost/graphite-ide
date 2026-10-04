#include <doctest/doctest.h>

#include "Core/Text/TextSelection.h"

#include <string>
#include <vector>

using namespace Core;

TEST_CASE("A press that has not moved selects nothing")
{
	TextSelection selection;
	selection.Begin({3, 4});
	CHECK(selection.IsEmpty());
	CHECK_FALSE(selection.SpanOn(3, 10).has_value());
}

TEST_CASE("A selection orders its ends whichever way it was dragged")
{
	TextSelection selection;
	selection.Begin({5, 2});
	selection.ExtendTo({3, 7});
	CHECK(selection.First() == TextPoint{3, 7});
	CHECK(selection.Last() == TextPoint{5, 2});
	CHECK(selection.Pressed() == TextPoint{5, 2});
}

TEST_CASE("A selection spans the tail of its first line, all of the middle and the head of its last")
{
	TextSelection selection;
	selection.Begin({1, 3});
	selection.ExtendTo({3, 2});
	CHECK(selection.SpanOn(1, 10) == ColumnSpan{3, 10});
	CHECK(selection.SpanOn(2, 6) == ColumnSpan{0, 6});
	CHECK(selection.SpanOn(3, 10) == ColumnSpan{0, 2});
	CHECK_FALSE(selection.SpanOn(0, 10).has_value());
	CHECK_FALSE(selection.SpanOn(4, 10).has_value());
}

TEST_CASE("A span is clamped to the line's length")
{
	TextSelection selection;
	selection.Begin({1, 8});
	selection.ExtendTo({1, 20});
	CHECK(selection.SpanOn(1, 5) == ColumnSpan{5, 5});
}

TEST_CASE("Extending keeps the pressed end, which is what Shift+click stretches from")
{
	TextSelection selection;
	selection.Select({2, 0}, {2, 4});
	selection.ExtendTo({4, 1});
	CHECK(selection.First() == TextPoint{2, 0});
	CHECK(selection.Last() == TextPoint{4, 1});
}

TEST_CASE("Clear empties the selection")
{
	TextSelection selection;
	selection.Select({0, 0}, {1, 1});
	selection.Clear();
	CHECK(selection.IsEmpty());
}

TEST_CASE("JoinSelected takes each line's selected part, one per output line")
{
	TextSelection selection;
	selection.Begin({10, 4});
	selection.ExtendTo({12, 3});
	const std::vector<SelectableLine> lines{{10, "int main()"}, {11, "{"}, {12, "}  // end"}};
	CHECK(JoinSelected(selection, lines) == "main()\n{\n}  ");
}

TEST_CASE("JoinSelected skips lines the caller does not show")
{
	TextSelection selection;
	selection.Begin({1, 0});
	selection.ExtendTo({3, 1});
	const std::vector<SelectableLine> lines{{1, "a"}, {3, "c"}};
	CHECK(JoinSelected(selection, lines) == "a\nc");
}

TEST_CASE("JoinSelected never splits a UTF-8 character")
{
	// "żółw": ż=2 bytes, ó=2, ł=2, w=1. A column inside 'ó' (byte 3) moves back to its start (2).
	TextSelection selection;
	selection.Begin({1, 3});
	selection.ExtendTo({1, 7});
	const std::vector<SelectableLine> lines{{1, "\xC5\xBC\xC3\xB3\xC5\x82w"}};
	CHECK(JoinSelected(selection, lines) == "\xC3\xB3\xC5\x82w");
}

TEST_CASE("JoinSelected of an empty selection is empty")
{
	TextSelection selection;
	const std::vector<SelectableLine> lines{{0, "text"}};
	CHECK(JoinSelected(selection, lines).empty());
}
