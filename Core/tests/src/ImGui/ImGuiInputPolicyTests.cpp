#include <doctest/doctest.h>

#include "Core/ImGui/ImGuiRenderer.h"

namespace {
	constexpr bool kPopupOpen = true;
	constexpr bool kNoPopupOpen = false;
}

TEST_CASE("Without a popup the GUI takes no keystroke, not even one typed into a field, which the application hands to the shortcuts that fire in text fields")
{
	CHECK_FALSE(Core::GuiConsumesKeyboard(kNoPopupOpen));
}

TEST_CASE("An open popup takes the keystroke, a field inside it too, because it is the only thing the user may answer")
{
	CHECK(Core::GuiConsumesKeyboard(kPopupOpen));
}
