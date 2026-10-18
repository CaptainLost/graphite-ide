#include <doctest/doctest.h>

#include "Theme/BuiltInSchemes.h"
#include "Theme/ChromePalette.h"

TEST_CASE("The chrome palette carries every colour of the scheme it was built from")
{
	Graphite::ResolvedScheme scheme = Graphite::DarkScheme();
	scheme.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Accent)] =
		Core::Color{1.0f, 0.5f, 0.0f, 1.0f};

	const Core::ChromePalette palette = Graphite::ChromePaletteFor(scheme);

	CHECK(palette.Accent.R == doctest::Approx(1.0f));
	CHECK(palette.Accent.G == doctest::Approx(0.5f));
	CHECK(palette.Background.R ==
		  doctest::Approx(
			  scheme.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Background)].R));
	CHECK(palette.Border.B ==
		  doctest::Approx(
			  scheme.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Border)].B));
}

TEST_CASE("The chrome palette takes its content colour from the editor background")
{
	Graphite::ResolvedScheme scheme = Graphite::DarkScheme();
	scheme.Editor[static_cast<std::size_t>(Graphite::EditorColor::Background)] = Core::Color{0.3f, 0.6f, 0.9f, 1.0f};

	const Core::ChromePalette palette = Graphite::ChromePaletteFor(scheme);

	CHECK(palette.Content.R == doctest::Approx(0.3f));
	CHECK(palette.Content.B == doctest::Approx(0.9f));
}
