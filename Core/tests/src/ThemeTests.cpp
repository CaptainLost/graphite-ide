#include <doctest/doctest.h>

#include "Core/ImGui/Theme.h"

#include <cstddef>

namespace {

	struct PaletteEntry
	{
		const char* Name;
		Core::Color Core::ChromePalette::* Member;
		float R;
		float G;
		float B;
		float A;
	};

	// All eight colours, all four channels. Anything less leaves most of the palette free to
	// move without a test noticing, which is the whole of what this file is for.
	constexpr PaletteEntry kExpected[] = {
		{"Background", &Core::ChromePalette::Background, 0.106f, 0.114f, 0.129f, 1.0f},
		{"Surface", &Core::ChromePalette::Surface, 0.145f, 0.157f, 0.176f, 1.0f},
		{"SurfaceHovered", &Core::ChromePalette::SurfaceHovered, 0.196f, 0.212f, 0.239f, 1.0f},
		{"SurfaceActive", &Core::ChromePalette::SurfaceActive, 0.243f, 0.263f, 0.298f, 1.0f},
		{"Accent", &Core::ChromePalette::Accent, 0.259f, 0.522f, 0.957f, 1.0f},
		{"Text", &Core::ChromePalette::Text, 0.839f, 0.859f, 0.886f, 1.0f},
		{"TextDisabled", &Core::ChromePalette::TextDisabled, 0.482f, 0.510f, 0.553f, 1.0f},
		{"Border", &Core::ChromePalette::Border, 0.204f, 0.220f, 0.247f, 1.0f},
	};
}

// The dark scheme in Graphite transcribes these bytes. Nothing can check that across the two
// projects — Theme/ is deliberately free of ImGui and Core is deliberately free of schemes —
// so this pins the side that would otherwise move silently.
TEST_CASE("The default chrome palette is the one the application shipped with")
{
	const Core::ChromePalette palette = Core::Theme::DefaultPalette();

	for (const PaletteEntry& expected : kExpected)
	{
		CAPTURE(expected.Name);

		const Core::Color& color = palette.*expected.Member;

		CHECK(color.R == doctest::Approx(expected.R).epsilon(0.01));
		CHECK(color.G == doctest::Approx(expected.G).epsilon(0.01));
		CHECK(color.B == doctest::Approx(expected.B).epsilon(0.01));
		CHECK(color.A == doctest::Approx(expected.A).epsilon(0.01));
	}
}

TEST_CASE("The border accessor answers from the default palette")
{
	const Core::Color border = Core::Theme::GetBorderColor();
	const Core::Color expected = Core::Theme::DefaultPalette().Border;

	CHECK(border.R == doctest::Approx(expected.R));
	CHECK(border.G == doctest::Approx(expected.G));
	CHECK(border.B == doctest::Approx(expected.B));
	CHECK(border.A == doctest::Approx(expected.A));
}
