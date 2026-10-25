#include <doctest/doctest.h>

#include "Core/Image/IcoDecoding.h"
#include "Core/Image/ImageDecoding.h"

#include "ImageFixtures.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

using namespace Core;

namespace {
	template<std::size_t Count>
	std::span<const std::uint8_t> Bytes(const std::uint8_t (&array)[Count])
	{
		return std::span<const std::uint8_t>{array, Count};
	}

	std::uint8_t Channel(const RgbaImage& image, int x, int y, int channel)
	{
		return image.Pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.Width) + static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(channel)];
	}

	void CheckPixel(const RgbaImage& image, int x, int y, int red, int green, int blue, int alpha)
	{
		CHECK(Channel(image, x, y, 0) == red);
		CHECK(Channel(image, x, y, 1) == green);
		CHECK(Channel(image, x, y, 2) == blue);
		CHECK(Channel(image, x, y, 3) == alpha);
	}
}

TEST_CASE("An ico shows its largest entry")
{
	std::size_t entryCount = 0;

	auto decoded = DecodeIco(Bytes(ImageFixtures::kIcoTwoEntries), entryCount);

	REQUIRE(decoded.HasValue());
	const RgbaImage& image = decoded.GetValue();
	CHECK(image.Width == 32);
	CHECK(image.Height == 32);
	CHECK(entryCount == 2);
	CheckPixel(image, 0, 0, 0, 200, 0, 255);
	CheckPixel(image, 31, 31, 0, 200, 0, 255);
}

TEST_CASE("An ico decodes through the image decoder as one frame and counts its entries")
{
	auto decoded = DecodeImage(Bytes(ImageFixtures::kIcoTwoEntries), ImageFormat::Ico, DecodeLimits{});

	REQUIRE(decoded.HasValue());
	const DecodedImage& image = decoded.GetValue();
	CHECK(image.Format == ImageFormat::Ico);
	CHECK(image.FileWidth == 32);
	CHECK(image.FileHeight == 32);
	CHECK(image.Frames.size() == 1);
	CHECK(image.FileFrameCount == 1);
	CHECK(image.EntryCount == 2);
}

TEST_CASE("An ico entry past the pixel limit is refused and one past the texture limit is scaled down")
{
	DecodeLimits tooMany;
	tooMany.MaximumPixels = 100;
	auto refused = DecodeImage(Bytes(ImageFixtures::kIcoTwoEntries), ImageFormat::Ico, tooMany);
	REQUIRE_FALSE(refused.HasValue());
	CHECK(refused.GetError().Code == ErrorCode::TooLarge);

	DecodeLimits narrow;
	narrow.MaximumSide = 8;
	auto scaled = DecodeImage(Bytes(ImageFixtures::kIcoTwoEntries), ImageFormat::Ico, narrow);
	REQUIRE(scaled.HasValue());
	CHECK(scaled.GetValue().FileWidth == 32);
	CHECK(scaled.GetValue().Frames.at(0).Image.Width == 8);
	CHECK(scaled.GetValue().Frames.at(0).Image.Height == 8);
}

TEST_CASE("A 32-bit ico entry keeps its alpha")
{
	std::size_t entryCount = 0;

	auto decoded = DecodeIco(Bytes(ImageFixtures::kIco32Bit2x2), entryCount);

	REQUIRE(decoded.HasValue());
	const RgbaImage& image = decoded.GetValue();
	CHECK(entryCount == 1);
	CHECK(image.Width == 2);
	CHECK(image.Height == 2);
	CheckPixel(image, 0, 0, 255, 0, 0, 255);
	CheckPixel(image, 1, 0, 0, 255, 0, 128);
	CheckPixel(image, 0, 1, 0, 0, 255, 0);
	CheckPixel(image, 1, 1, 255, 255, 255, 255);
}

TEST_CASE("A 32-bit ico entry with no alpha at all takes its transparency from the mask")
{
	std::size_t entryCount = 0;

	auto decoded = DecodeIco(Bytes(ImageFixtures::kIco32BitNoAlpha2x2), entryCount);

	REQUIRE(decoded.HasValue());
	const RgbaImage& image = decoded.GetValue();
	CheckPixel(image, 0, 0, 255, 0, 0, 255);
	CheckPixel(image, 1, 0, 0, 255, 0, 255);
	CheckPixel(image, 0, 1, 0, 0, 255, 255);
	CheckPixel(image, 1, 1, 255, 255, 255, 0);
}

TEST_CASE("The AND mask makes pixels of a 24-bit entry transparent")
{
	std::size_t entryCount = 0;

	auto decoded = DecodeIco(Bytes(ImageFixtures::kIco24Bit2x2), entryCount);

	REQUIRE(decoded.HasValue());
	const RgbaImage& image = decoded.GetValue();
	CheckPixel(image, 0, 0, 255, 0, 0, 255);
	CheckPixel(image, 1, 0, 0, 255, 0, 0);
	CheckPixel(image, 0, 1, 0, 0, 255, 255);
	CheckPixel(image, 1, 1, 255, 255, 255, 255);
}

TEST_CASE("A paletted 8-bit ico entry decodes through its palette")
{
	std::size_t entryCount = 0;

	auto decoded = DecodeIco(Bytes(ImageFixtures::kIco8Bit2x2), entryCount);

	REQUIRE(decoded.HasValue());
	const RgbaImage& image = decoded.GetValue();
	CheckPixel(image, 0, 0, 255, 0, 0, 255);
	CheckPixel(image, 1, 0, 0, 255, 0, 255);
	CheckPixel(image, 0, 1, 0, 0, 255, 255);
	CheckPixel(image, 1, 1, 0, 0, 0, 255);
}

TEST_CASE("A 1-bit ico entry decodes with its mask")
{
	std::size_t entryCount = 0;

	auto decoded = DecodeIco(Bytes(ImageFixtures::kIco1Bit2x2), entryCount);

	REQUIRE(decoded.HasValue());
	const RgbaImage& image = decoded.GetValue();
	CheckPixel(image, 0, 0, 0, 0, 0, 255);
	CheckPixel(image, 1, 0, 255, 255, 255, 255);
	CheckPixel(image, 0, 1, 255, 255, 255, 0);
	CheckPixel(image, 1, 1, 0, 0, 0, 255);
}

TEST_CASE("An ico with no entries is unreadable")
{
	std::size_t entryCount = 7;

	auto decoded = DecodeIco(Bytes(ImageFixtures::kIcoNoEntries), entryCount);

	REQUIRE_FALSE(decoded.HasValue());
	CHECK(decoded.GetError().Code == ErrorCode::InvalidArgument);
}

TEST_CASE("A truncated ico is unreadable")
{
	std::size_t entryCount = 0;
	const std::span<const std::uint8_t> whole = Bytes(ImageFixtures::kIco24Bit2x2);

	auto decoded = DecodeIco(whole.first(whole.size() - 8), entryCount);

	REQUIRE_FALSE(decoded.HasValue());
	CHECK(decoded.GetError().Code == ErrorCode::InvalidArgument);
}
