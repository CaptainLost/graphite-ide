#include <doctest/doctest.h>

#include "Core/ImGui/Theme.h"

// The dark scheme in Graphite transcribes these bytes. Nothing can check that across the two
// projects — Theme/ is deliberately free of ImGui and Core is deliberately free of schemes —
// so this pins the side that would otherwise move silently.
TEST_CASE("The default chrome palette is the one the application shipped with")
{
	const Core::ChromePalette palette = Core::Theme::DefaultPalette();

	CHECK(palette.Background.R == doctest::Approx(0.106f).epsilon(0.01));
	CHECK(palette.Accent.B == doctest::Approx(0.957f).epsilon(0.01));
	CHECK(palette.Text.G == doctest::Approx(0.859f).epsilon(0.01));
	CHECK(palette.Border.R == doctest::Approx(0.204f).epsilon(0.01));
}

TEST_CASE("The border accessor answers from the default palette")
{
	CHECK(Core::Theme::GetBorderColor().R ==
		  doctest::Approx(Core::Theme::DefaultPalette().Border.R));
	CHECK(Core::Theme::GetBorderColor().B ==
		  doctest::Approx(Core::Theme::DefaultPalette().Border.B));
}
