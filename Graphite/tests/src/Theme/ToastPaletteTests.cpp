#include <doctest/doctest.h>

#include "Theme/BuiltInSchemes.h"
#include "Theme/ToastPalette.h"

namespace {
	bool Same(const Core::Color& a, const Core::Color& b)
	{
		return a.R == doctest::Approx(b.R) && a.G == doctest::Approx(b.G) && a.B == doctest::Approx(b.B) &&
			   a.A == doctest::Approx(b.A);
	}
}

TEST_CASE("The toast palette takes the chrome's surface and text and the diagnostic and added colours")
{
	// Every colour differs from the others in every channel, so a palette that swapped any two
	// of its fields fails here.
	const Core::Color surface{0.11f, 0.12f, 0.13f, 0.91f};
	const Core::Color text{0.21f, 0.22f, 0.23f, 0.92f};
	const Core::Color success{0.31f, 0.32f, 0.33f, 0.93f};
	const Core::Color warning{0.41f, 0.42f, 0.43f, 0.94f};
	const Core::Color error{0.51f, 0.52f, 0.53f, 0.95f};

	Graphite::ResolvedScheme scheme = Graphite::DarkScheme();
	scheme.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Surface)] = surface;
	scheme.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Text)] = text;
	scheme.Git[static_cast<std::size_t>(Graphite::GitColor::GutterAdded)] = success;
	scheme.Editor[static_cast<std::size_t>(Graphite::EditorColor::DiagnosticWarning)] = warning;
	scheme.Editor[static_cast<std::size_t>(Graphite::EditorColor::DiagnosticError)] = error;

	const Core::ToastPalette palette = Graphite::ToastPaletteFor(scheme);

	CHECK(Same(palette.Surface, surface));
	CHECK(Same(palette.Text, text));
	CHECK(Same(palette.Success, success));
	CHECK(Same(palette.Warning, warning));
	CHECK(Same(palette.Error, error));
}
