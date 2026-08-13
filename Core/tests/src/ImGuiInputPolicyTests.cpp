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

TEST_CASE("An active item takes the keystroke")
{
	// A text field being typed into is the case worth protecting: naming a file must not
	// also run the command bound to the letters of the name.
	CHECK(Core::GuiConsumesKeyboard(true, false));
}

TEST_CASE("An open modal takes the keystroke")
{
	// A modal is on screen precisely because it is the only thing the user may answer.
	CHECK(Core::GuiConsumesKeyboard(false, true));
}

TEST_CASE("An active item inside a modal still takes the keystroke")
{
	CHECK(Core::GuiConsumesKeyboard(true, true));
}
