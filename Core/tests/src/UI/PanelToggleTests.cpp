#include <doctest/doctest.h>

#include "Core/UI/PanelToggle.h"

TEST_CASE("A hidden panel is shown and brought forward")
{
	CHECK(Core::ChoosePanelToggle(false, false) == Core::PanelToggle::ShowAndBringForward);
}

TEST_CASE("A visible panel behind another tab is brought forward rather than hidden")
{
	CHECK(Core::ChoosePanelToggle(true, false) == Core::PanelToggle::BringForward);
}

TEST_CASE("A visible panel that was drawn is hidden")
{
	CHECK(Core::ChoosePanelToggle(true, true) == Core::PanelToggle::Hide);
}
