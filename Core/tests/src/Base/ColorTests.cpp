#include <doctest/doctest.h>

#include <cstdint>
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

TEST_CASE("The channels land in a Windows COLORREF's order, 0x00BBGGRR, with red lowest and blue highest")
{
	CHECK(PackBgr(Color{.R = 1.0f, .G = 0.0f, .B = 0.0f, .A = 1.0f}) == 0x0000FFu);
	CHECK(PackBgr(Color{.R = 0.0f, .G = 1.0f, .B = 0.0f, .A = 1.0f}) == 0x00FF00u);
	CHECK(PackBgr(Color{.R = 0.0f, .G = 0.0f, .B = 1.0f, .A = 1.0f}) == 0xFF0000u);
}

TEST_CASE("Alpha is dropped, because the border it colours has none")
{
	CHECK(PackBgr(Color{.R = 1.0f, .G = 1.0f, .B = 1.0f, .A = 0.0f}) == 0xFFFFFFu);
	CHECK(PackBgr(Color{.R = 1.0f, .G = 1.0f, .B = 1.0f, .A = 1.0f}) == 0xFFFFFFu);
}

TEST_CASE("Channels round to the nearest byte rather than truncating, so 0.247 packs to the 63 the theme paints and not to 62")
{
	const float channelOf62Point985Bytes = 0.247f;

	CHECK(PackBgr(Color{.R = channelOf62Point985Bytes, .G = 0.0f, .B = 0.0f, .A = 1.0f}) == 63u);
	CHECK(PackBgr(Color{.R = 0.5f, .G = 0.0f, .B = 0.0f, .A = 1.0f}) == 128u);
}

TEST_CASE("Channels outside the unit range clamp to the end they passed")
{
	CHECK(PackBgr(Color{.R = -1.0f, .G = 2.0f, .B = 0.0f, .A = 1.0f}) == 0x00FF00u);
}

TEST_CASE("The theme's border colour packs to RGB(36, 39, 44), the only check on the colour the window border is given")
{
	const std::uint32_t red36Green39Blue44 = 0x2C2724u;

	CHECK(PackBgr(Theme::GetBorderColor()) == red36Green39Blue44);
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
	CHECK(ToHexColor(Color{.R = 1.0f, .G = 0.0f, .B = 0.0f, .A = 1.0f}) == "FF0000");
	CHECK(ToHexColor(Color{.R = 0.0f, .G = 0.0f, .B = 0.0f, .A = 0.0f}) == "00000000");
}

TEST_CASE("Every channel value round-trips through parse and format exactly so a saved scheme reads back unchanged")
{
	constexpr int kLargestByteValue = 255;
	constexpr std::size_t kSixHexDigitsAndTerminator = 7;

	for (int byteValue = 0; byteValue <= kLargestByteValue; ++byteValue)
	{
		char greyHexText[kSixHexDigitsAndTerminator] = {};
		std::snprintf(greyHexText, sizeof(greyHexText), "%02X%02X%02X", byteValue, byteValue, byteValue);

		const std::optional<Color> parsed = ParseHexColor(greyHexText);

		REQUIRE(parsed.has_value());
		CHECK(ToHexColor(*parsed) == greyHexText);
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

	// ImGui's +0.5 rounding turns this channel into 1; lround turns its 0.49999997 of a byte into 0.
	const float channelJustUnderHalfAByte = 0.0019607842f;
	CHECK(Core::ChannelToByte(channelJustUnderHalfAByte) == 0);

	CHECK(Core::ChannelFromByte(0) == 0.0f);
	CHECK(Core::ChannelFromByte(255) == 1.0f);

	constexpr int kByteValueCount = 256;

	for (int byteValue = 0; byteValue < kByteValueCount; ++byteValue)
	{
		CHECK(Core::ChannelToByte(Core::ChannelFromByte(byteValue)) == byteValue);
	}
}
