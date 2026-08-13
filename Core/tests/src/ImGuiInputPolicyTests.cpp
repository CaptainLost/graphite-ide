#include <doctest/doctest.h>

#include "Core/ImGui/ImGuiRenderer.h"

TEST_CASE("Keyboard navigation being available does not entitle the GUI to a keystroke")
{
	// The regression this exists for: the decision was once made on ImGui's
	// WantCaptureKeyboard, which is true whenever keyboard navigation is active — and
	// ImGuiConfigFlags_NavEnableKeyboard makes that true for any focused window that allows
	// navigation, which is nearly all of them. Every keystroke was consumed before it could
	// reach a panel recording a shortcut, so a chord could be armed but never finished.
	CHECK_FALSE(Core::GuiConsumesKeyboard(false, false));
}

TEST_CASE("A field being typed into takes the keystroke")
{
	// The case worth protecting: naming a file must not also run the commands bound to the
	// letters of the name.
	CHECK(Core::GuiConsumesKeyboard(true, false));
}

TEST_CASE("A widget merely being interacted with does not take the keystroke")
{
	// Held mouse buttons make an item active for as long as they are held, so deciding on
	// "some item is active" stopped the keyboard working for the whole of a selection drag
	// in an editor. Wanting text input is the narrower question, and the right one.
	const bool draggingASelection = false; // an active item, but nothing wants text

	CHECK_FALSE(Core::GuiConsumesKeyboard(draggingASelection, false));
}

TEST_CASE("An open modal takes the keystroke")
{
	// A modal is on screen precisely because it is the only thing the user may answer.
	CHECK(Core::GuiConsumesKeyboard(false, true));
}

TEST_CASE("A field inside a popup still takes the keystroke")
{
	CHECK(Core::GuiConsumesKeyboard(true, true));
}
