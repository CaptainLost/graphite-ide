#include <doctest/doctest.h>

#include "Core/ImGui/ToastStyle.h"

namespace {
	Core::ToastPalette DistinctPalette()
	{
		return {
			.Surface = {.R = 0.1f},
			.Text = {.R = 1.0f, .G = 1.0f, .B = 1.0f},
			.TextDim = {.R = 0.5f, .G = 0.5f, .B = 0.5f},
			.Success = {.G = 1.0f},
			.Warning = {.R = 1.0f, .G = 1.0f},
			.Error = {.R = 1.0f},
		};
	}
}

TEST_CASE("Each toast kind takes its accent from the palette's colour for that kind")
{
	const Core::ToastPalette palette = DistinctPalette();

	CHECK(&Core::ToastAccentOf(palette, Core::ToastKind::Success) == &palette.Success);
	CHECK(&Core::ToastAccentOf(palette, Core::ToastKind::Warning) == &palette.Warning);
	CHECK(&Core::ToastAccentOf(palette, Core::ToastKind::Error) == &palette.Error);
}

TEST_CASE("A toast's message sits most of the way from the dim text towards the full text")
{
	const Core::Color message = Core::ToastMessageColour(DistinctPalette());

	CHECK(message.R == doctest::Approx(0.5f + 0.5f * 0.85f));
	CHECK(message.A == doctest::Approx(1.0f));
}
