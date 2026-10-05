#include <doctest/doctest.h>

#include <cstdio>
#include <limits>
#include <optional>

#include "Core/Base/Color.h"
#include "Core/ImGui/Theme.h"

using Core::Color;
using Core::PackBgr;
using Core::ParseHexColor;
using Core::Theme;
using Core::ToHexColor;

TEST_CASE("The channels land in the order the system reads them, blue highest")
{
	CHECK(PackBgr(Color{1.0f, 0.0f, 0.0f, 1.0f}) == 0x0000FFu);
	CHECK(PackBgr(Color{0.0f, 1.0f, 0.0f, 1.0f}) == 0x00FF00u);
	CHECK(PackBgr(Color{0.0f, 0.0f, 1.0f, 1.0f}) == 0xFF0000u);
}

TEST_CASE("Alpha is dropped, because the border it colours has none")
{
	CHECK(PackBgr(Color{1.0f, 1.0f, 1.0f, 0.0f}) == 0xFFFFFFu);
	CHECK(PackBgr(Color{1.0f, 1.0f, 1.0f, 1.0f}) == 0xFFFFFFu);
}

TEST_CASE("Channels round to nearest rather than truncating")
{
	// 0.247 is 62.985 of 255: truncation would say 62, which is a different colour from
	// the one the theme paints the same float with.
	CHECK(PackBgr(Color{0.247f, 0.0f, 0.0f, 1.0f}) == 63u);
	CHECK(PackBgr(Color{0.5f, 0.0f, 0.0f, 1.0f}) == 128u);
}

TEST_CASE("Channels outside the unit range clamp to the end they passed")
{
	CHECK(PackBgr(Color{-1.0f, 2.0f, 0.0f, 1.0f}) == 0x00FF00u);
}

TEST_CASE("The window border is the theme's border colour, unchanged")
{
	// 0x3F3834 is the RGB(52, 56, 63) that DWM used to be handed as a literal in
	// the frame, back when the theme and the frame each spelled this colour out.
	// The frame now packs whatever the theme says, so this is what pins the two together
	// — nothing else in the process can observe the colour a window border was given.
	CHECK(PackBgr(Theme::GetBorderColor()) == 0x3F3834u);
}

TEST_CASE("A six-character hex colour parses to full opacity")
{
	const std::optional<Color> parsed = ParseHexColor("C586C0");

	REQUIRE(parsed.has_value());
	CHECK(parsed->R == doctest::Approx(197.0f / 255.0f));
	CHECK(parsed->G == doctest::Approx(134.0f / 255.0f));
	CHECK(parsed->B == doctest::Approx(192.0f / 255.0f));
	CHECK(parsed->A == doctest::Approx(1.0f));
}

TEST_CASE("An eight-character hex colour carries its alpha")
{
	const std::optional<Color> parsed = ParseHexColor("264F7844");

	REQUIRE(parsed.has_value());
	CHECK(parsed->A == doctest::Approx(68.0f / 255.0f));
}

TEST_CASE("Hex parsing ignores case")
{
	const std::optional<Color> upper = ParseHexColor("ABCDEF");
	const std::optional<Color> lower = ParseHexColor("abcdef");

	REQUIRE(upper.has_value());
	REQUIRE(lower.has_value());
	CHECK(upper->R == doctest::Approx(lower->R));
	CHECK(upper->G == doctest::Approx(lower->G));
	CHECK(upper->B == doctest::Approx(lower->B));
}

// A scheme file is hand-editable, so every one of these is something a person will actually
// type. None of them may produce a colour.
TEST_CASE("Hex parsing rejects anything that is not six or eight hex digits")
{
	CHECK_FALSE(ParseHexColor("").has_value());
	CHECK_FALSE(ParseHexColor("C586C").has_value());
	CHECK_FALSE(ParseHexColor("C586C0F").has_value());
	CHECK_FALSE(ParseHexColor("C586C0FFF").has_value());
	CHECK_FALSE(ParseHexColor("#C586C0F").has_value());
	CHECK_FALSE(ParseHexColor("GGGGGG").has_value());
	CHECK_FALSE(ParseHexColor("C586 0").has_value());
}

TEST_CASE("A fully opaque colour formats as six characters, a translucent one as eight")
{
	CHECK(ToHexColor(Color{1.0f, 0.0f, 0.0f, 1.0f}) == "FF0000");
	CHECK(ToHexColor(Color{0.0f, 0.0f, 0.0f, 0.0f}) == "00000000");
}

// Color stores floats, so this is the property that decides whether a scheme survives being
// saved: every byte value has to come back as itself. Proved over the whole range rather than
// sampled, because it is cheap and the alternative is an estimate.
TEST_CASE("Every channel value round-trips through parse and format exactly")
{
	for (int value = 0; value <= 255; ++value)
	{
		char text[7] = {};
		std::snprintf(text, sizeof(text), "%02X%02X%02X", value, value, value);

		const std::optional<Color> parsed = ParseHexColor(text);

		REQUIRE(parsed.has_value());
		CHECK(ToHexColor(*parsed) == text);
	}
}

TEST_CASE("A channel becomes the nearest byte, clamped, and a byte becomes its channel")
{
	CHECK(Core::ChannelToByte(0.0f) == 0);
	CHECK(Core::ChannelToByte(1.0f) == 255);
	CHECK(Core::ChannelToByte(0.5f) == 128);
	CHECK(Core::ChannelToByte(-0.0f) == 0);
	CHECK(Core::ChannelToByte(-3.0f) == 0);
	CHECK(Core::ChannelToByte(7.0f) == 255);
	CHECK(Core::ChannelToByte(std::numeric_limits<float>::infinity()) == 255);

	// 0.0019607842f * 255 is 0.49999997f: lround gives 0 where ImGui's +0.5 rounding gives 1.
	CHECK(Core::ChannelToByte(0.0019607842f) == 0);

	CHECK(Core::ChannelFromByte(0) == 0.0f);
	CHECK(Core::ChannelFromByte(255) == 1.0f);

	for (int byteValue = 0; byteValue < 256; ++byteValue)
	{
		CHECK(Core::ChannelToByte(Core::ChannelFromByte(byteValue)) == byteValue);
	}
}
