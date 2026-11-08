#include <doctest/doctest.h>

#include "Terminal/ReadableColour.h"

#include <initializer_list>

namespace {
	constexpr ImU32 kLightBackground = IM_COL32(0xFA, 0xFA, 0xFA, 0xFF);
	constexpr ImU32 kDarkBackground = IM_COL32(0x1E, 0x1E, 0x1E, 0xFF);
	constexpr ImU32 kDarkText = IM_COL32(0x24, 0x29, 0x2E, 0xFF);
	constexpr ImU32 kLightText = IM_COL32(0xD4, 0xD4, 0xD4, 0xFF);
	constexpr ImU32 kXtermYellow = IM_COL32(0xCD, 0xCD, 0x00, 0xFF);
	constexpr ImU32 kXtermWhite = IM_COL32(0xE5, 0xE5, 0xE5, 0xFF);
}

TEST_CASE("Black on white is the widest contrast and a colour against itself the narrowest")
{
	CHECK(Graphite::ContrastRatio(IM_COL32(0, 0, 0, 255), IM_COL32(255, 255, 255, 255)) == doctest::Approx(21.0f));
	CHECK(Graphite::ContrastRatio(kXtermYellow, kXtermYellow) == doctest::Approx(1.0f));
}

TEST_CASE("xterm's yellow and white are moved until they read on a light background")
{
	for (const ImU32 colour : {kXtermYellow, kXtermWhite})
	{
		REQUIRE(Graphite::ContrastRatio(colour, kLightBackground) < Graphite::kReadableContrast);

		const ImU32 readable = Graphite::ReadableOn(colour, kLightBackground, kDarkText);

		CHECK(Graphite::ContrastRatio(readable, kLightBackground) >= Graphite::kReadableContrast);
	}
}

TEST_CASE("A colour that already reads is left as it is")
{
	CHECK(Graphite::ReadableOn(kXtermYellow, kDarkBackground, kLightText) == kXtermYellow);
	CHECK(Graphite::ReadableOn(kDarkText, kLightBackground, kDarkText) == kDarkText);
}

TEST_CASE("A moved colour keeps its alpha")
{
	const ImU32 halfYellow = IM_COL32(0xCD, 0xCD, 0x00, 0x80);

	CHECK((Graphite::ReadableOn(halfYellow, kLightBackground, kDarkText) >> IM_COL32_A_SHIFT) == 0x80u);
}
