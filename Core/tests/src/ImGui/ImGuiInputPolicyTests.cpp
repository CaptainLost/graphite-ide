#include <doctest/doctest.h>

#include "Core/ImGui/ImGuiRenderer.h"

namespace {
	constexpr bool kWantsTextInput = true;
	constexpr bool kWantsNoTextInput = false;
	constexpr bool kPopupOpen = true;
	constexpr bool kNoPopupOpen = false;
}

TEST_CASE("Keyboard navigation being available does not entitle the GUI to a keystroke")
{
	CHECK_FALSE(Core::GuiConsumesKeyboard(kWantsNoTextInput, kNoPopupOpen));
}

TEST_CASE("A field being typed into takes the keystroke, so naming a file runs no command bound to a letter of the name")
{
	CHECK(Core::GuiConsumesKeyboard(kWantsTextInput, kNoPopupOpen));
}

TEST_CASE("A widget merely being interacted with, such as a held selection drag, does not take the keystroke")
{
	const bool draggingASelection = kWantsNoTextInput;

	CHECK_FALSE(Core::GuiConsumesKeyboard(draggingASelection, kNoPopupOpen));
}

TEST_CASE("An open modal takes the keystroke because it is the only thing the user may answer")
{
	CHECK(Core::GuiConsumesKeyboard(kWantsNoTextInput, kPopupOpen));
}

TEST_CASE("A field inside a popup still takes the keystroke")
{
	CHECK(Core::GuiConsumesKeyboard(kWantsTextInput, kPopupOpen));
}
