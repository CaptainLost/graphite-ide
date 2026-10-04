#include <doctest/doctest.h>

#include "Core/ImGui/SelectionGesture.h"

using namespace Core;

TEST_CASE("A single press over a line begins a selection")
{
	CHECK(DecidePress({1, false, true, false}) == SelectionAction::Begin);
}

TEST_CASE("Shift with a selection extends it instead of starting again")
{
	CHECK(DecidePress({1, true, true, true}) == SelectionAction::Extend);
	CHECK(DecidePress({1, true, true, false}) == SelectionAction::Begin);
}

TEST_CASE("A double press selects a word and a triple press a line")
{
	CHECK(DecidePress({2, false, true, false}) == SelectionAction::SelectWord);
	CHECK(DecidePress({3, false, true, true}) == SelectionAction::SelectLine);
	CHECK(DecidePress({4, false, true, true}) == SelectionAction::SelectLine);
}

TEST_CASE("A press off any line clears without starting a sweep")
{
	CHECK(DecidePress({1, false, false, true}) == SelectionAction::Clear);
	CHECK(DecidePress({2, false, false, true}) == SelectionAction::Clear);
}

TEST_CASE("No press does nothing")
{
	CHECK(DecidePress({0, true, true, true}) == SelectionAction::None);
}

TEST_CASE("A word or a line press acts on the character under the pointer, not the nearest boundary")
{
	SelectionSource source;
	source.CharacterAt = [](ImVec2) { return std::optional<TextPoint>(TextPoint{4, 1}); };

	const std::optional<TextPoint> boundary = TextPoint{4, 2};

	CHECK(PressPoint(SelectionAction::SelectWord, source, ImVec2(0.0f, 0.0f), boundary) == TextPoint{4, 1});
	CHECK(PressPoint(SelectionAction::SelectLine, source, ImVec2(0.0f, 0.0f), boundary) == TextPoint{4, 1});

	// A single press and a Shift+click keep the boundary.
	CHECK(PressPoint(SelectionAction::Begin, source, ImVec2(0.0f, 0.0f), boundary) == boundary);
	CHECK(PressPoint(SelectionAction::Extend, source, ImVec2(0.0f, 0.0f), boundary) == boundary);
}

TEST_CASE("A word press falls back to the boundary when the owner names no character")
{
	const std::optional<TextPoint> boundary = TextPoint{2, 3};

	SelectionSource unset;
	CHECK(PressPoint(SelectionAction::SelectWord, unset, ImVec2(0.0f, 0.0f), boundary) == boundary);

	SelectionSource silent;
	silent.CharacterAt = [](ImVec2) { return std::optional<TextPoint>{}; };
	CHECK(PressPoint(SelectionAction::SelectWord, silent, ImVec2(0.0f, 0.0f), boundary) == boundary);
}
