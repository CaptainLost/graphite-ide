#include <doctest/doctest.h>

#include "Theme/BuiltInSchemes.h"
#include "Theme/ToastPalette.h"

namespace {
	bool Same(const Core::Color& actual, const Core::Color& expected)
	{
		return actual.R == doctest::Approx(expected.R) && actual.G == doctest::Approx(expected.G) &&
			   actual.B == doctest::Approx(expected.B) && actual.A == doctest::Approx(expected.A);
	}
}

TEST_CASE("Every colour differs from the others in every channel, so swapping two fields fails: the toast palette takes the chrome's surface, text and dimmed text and the diagnostic and added colours")
{
	const Core::Color surface{0.11f, 0.12f, 0.13f, 0.81f};
	const Core::Color text{0.31f, 0.32f, 0.33f, 0.83f};
	const Core::Color textDim{0.41f, 0.42f, 0.43f, 0.84f};
	const Core::Color success{0.51f, 0.52f, 0.53f, 0.85f};
	const Core::Color warning{0.61f, 0.62f, 0.63f, 0.86f};
	const Core::Color error{0.71f, 0.72f, 0.73f, 0.87f};

	Graphite::ResolvedScheme scheme = Graphite::DarkScheme();
	scheme.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Surface)] = surface;
	scheme.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::Text)] = text;
	scheme.Chrome[static_cast<std::size_t>(Graphite::ChromeColor::TextDisabled)] = textDim;
	scheme.Git[static_cast<std::size_t>(Graphite::GitColor::GutterAdded)] = success;
	scheme.Editor[static_cast<std::size_t>(Graphite::EditorColor::DiagnosticWarning)] = warning;
	scheme.Editor[static_cast<std::size_t>(Graphite::EditorColor::DiagnosticError)] = error;

	const Core::ToastPalette palette = Graphite::ToastPaletteFor(scheme);

	CHECK(Same(palette.Surface, surface));
	CHECK(Same(palette.Text, text));
	CHECK(Same(palette.TextDim, textDim));
	CHECK(Same(palette.Success, success));
	CHECK(Same(palette.Warning, warning));
	CHECK(Same(palette.Error, error));
}
