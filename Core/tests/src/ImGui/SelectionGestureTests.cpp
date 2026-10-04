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
