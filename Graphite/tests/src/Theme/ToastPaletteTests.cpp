#include <doctest/doctest.h>

#include "Theme/BuiltInSchemes.h"
#include "Theme/ToastPalette.h"

TEST_CASE("The toast palette takes the chrome's surface and text and the diagnostic and added colours")
{
	Graphite::ResolvedScheme scheme = Graphite::DarkScheme();
	scheme.Editor[static_cast<std::size_t>(Graphite::EditorColor::DiagnosticError)] = Core::Color{0.9f, 0.1f, 0.1f, 1.0f};
	scheme.Editor[static_cast<std::size_t>(Graphite::EditorColor::DiagnosticWarning)] = Core::Color{0.9f, 0.8f, 0.1f, 1.0f};
	scheme.Git[static_cast<std::size_t>(Graphite::GitColor::GutterAdded)] = Core::Color{0.1f, 0.8f, 0.3f, 1.0f};

	const Core::ToastPalette palette = Graphite::ToastPaletteFor(scheme);

	CHECK(palette.Error.R == doctest::Approx(0.9f));
	CHECK(palette.Warning.G == doctest::Approx(0.8f));
	CHECK(palette.Success.G == doctest::Approx(0.8f));
	CHECK(palette.Surface.R ==
		  doctest::Approx(scheme.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Surface)].R));
	CHECK(palette.Text.B == doctest::Approx(scheme.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Text)].B));
}
