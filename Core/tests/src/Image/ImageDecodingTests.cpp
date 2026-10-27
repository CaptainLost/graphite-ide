#include <doctest/doctest.h>

#include "Core/Image/ImageDecoding.h"

#include "ImageFixtures.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

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

	void CheckPixel(const RgbaImage& image, int x, int y, int red, int green, int blue, int alpha, int tolerance = 0)
	{
		CHECK(std::abs(Channel(image, x, y, 0) - red) <= tolerance);
		CHECK(std::abs(Channel(image, x, y, 1) - green) <= tolerance);
		CHECK(std::abs(Channel(image, x, y, 2) - blue) <= tolerance);
		CHECK(std::abs(Channel(image, x, y, 3) - alpha) <= tolerance);
	}
}

TEST_CASE("A png decodes to its size and pixels")
{
	auto decoded = DecodeImage(Bytes(ImageFixtures::kPng2x2), ImageFormat::Png, DecodeLimits{});

	REQUIRE(decoded.HasValue());
	const DecodedImage& image = decoded.GetValue();
	CHECK(image.Format == ImageFormat::Png);
	CHECK(image.FileWidth == 2);
	CHECK(image.FileHeight == 2);
	REQUIRE(image.Frames.size() == 1);
	const RgbaImage& pixels = image.Frames[0].Image;
	CHECK(pixels.Width == 2);
	CHECK(pixels.Height == 2);
	CHECK(pixels.ByteSize() == 16);
	CheckPixel(pixels, 0, 0, 255, 0, 0, 255);
	CheckPixel(pixels, 1, 0, 0, 255, 0, 255);
	CheckPixel(pixels, 0, 1, 0, 0, 255, 255);
	CHECK(Channel(pixels, 1, 1, 3) == 0);
}

TEST_CASE("A grayscale png with transparency decodes to RGBA")
{
	auto decoded = DecodeImage(Bytes(ImageFixtures::kGrayAlphaPng2x1), ImageFormat::Png, DecodeLimits{});

	REQUIRE(decoded.HasValue());
	const RgbaImage& pixels = decoded.GetValue().Frames.at(0).Image;
	CHECK(pixels.Width == 2);
	CHECK(pixels.Height == 1);
	CheckPixel(pixels, 0, 0, 100, 100, 100, 255);
	CheckPixel(pixels, 1, 0, 200, 200, 200, 128);
}

TEST_CASE("A jpeg decodes to its size")
{
	auto decoded = DecodeImage(Bytes(ImageFixtures::kJpeg8x8), ImageFormat::Jpeg, DecodeLimits{});

	REQUIRE(decoded.HasValue());
	const DecodedImage& image = decoded.GetValue();
	CHECK(image.FileWidth == 8);
	CHECK(image.FileHeight == 8);
	const RgbaImage& pixels = image.Frames.at(0).Image;
	CHECK(pixels.Width == 8);
	CHECK(pixels.Height == 8);
	CheckPixel(pixels, 0, 0, 200, 40, 40, 255, 8);
	CheckPixel(pixels, 7, 7, 200, 40, 40, 255, 8);
}

TEST_CASE("A bmp decodes to its size and pixels")
{
	auto decoded = DecodeImage(Bytes(ImageFixtures::kBmp2x2), ImageFormat::Bmp, DecodeLimits{});

	REQUIRE(decoded.HasValue());
	const RgbaImage& pixels = decoded.GetValue().Frames.at(0).Image;
	CHECK(pixels.Width == 2);
	CHECK(pixels.Height == 2);
	CheckPixel(pixels, 0, 0, 255, 0, 0, 255);
	CheckPixel(pixels, 1, 0, 0, 255, 0, 255);
	CheckPixel(pixels, 0, 1, 0, 0, 255, 255);
	CheckPixel(pixels, 1, 1, 255, 255, 255, 255);
}

TEST_CASE("An animated gif decodes every frame with its delay")
{
	auto decoded = DecodeImage(Bytes(ImageFixtures::kGif2Frames2x2), ImageFormat::Gif, DecodeLimits{});

	REQUIRE(decoded.HasValue());
	const DecodedImage& image = decoded.GetValue();
	REQUIRE(image.Frames.size() == 2);
	CHECK(image.FileFrameCount == 2);
	CHECK(image.Frames[0].Delay == std::chrono::milliseconds{50});
	// A delay under 20 ms is a browser convention for "unset", read as 100 ms.
	CHECK(image.Frames[1].Delay == std::chrono::milliseconds{100});
	CheckPixel(image.Frames[0].Image, 0, 0, 255, 0, 0, 255);
	CheckPixel(image.Frames[1].Image, 1, 1, 0, 0, 255, 255);
}

TEST_CASE("A gif past the animation budget keeps only its first frame")
{
	DecodeLimits limits;
	limits.MaximumAnimationBytes = 20;

	auto decoded = DecodeImage(Bytes(ImageFixtures::kGif2Frames2x2), ImageFormat::Gif, limits);

	REQUIRE(decoded.HasValue());
	const DecodedImage& image = decoded.GetValue();
	CHECK(image.Frames.size() == 1);
	CHECK(image.FileFrameCount == 2);
	CheckPixel(image.Frames[0].Image, 0, 0, 255, 0, 0, 255);
}

TEST_CASE("An image past the pixel limit is refused before it is decoded")
{
	DecodeLimits limits;
	limits.MaximumPixels = 3;

	auto decoded = DecodeImage(Bytes(ImageFixtures::kPng2x2), ImageFormat::Png, limits);

	REQUIRE_FALSE(decoded.HasValue());
	CHECK(decoded.GetError().Code == ErrorCode::TooLarge);
	CHECK(decoded.GetError().Message == "Image is too large to preview (2 × 2)");
}

TEST_CASE("An image larger than the texture limit is scaled down to fit")
{
	DecodeLimits limits;
	limits.MaximumSide = 1;

	auto decoded = DecodeImage(Bytes(ImageFixtures::kPng2x2), ImageFormat::Png, limits);

	REQUIRE(decoded.HasValue());
	const DecodedImage& image = decoded.GetValue();
	CHECK(image.FileWidth == 2);
	CHECK(image.FileHeight == 2);
	CHECK(image.Frames.at(0).Image.Width == 1);
	CHECK(image.Frames.at(0).Image.Height == 1);
	CHECK(image.Frames.at(0).Image.Pixels.size() == 4);
}

TEST_CASE("Scaling keeps the aspect ratio and leaves a small image alone")
{
	RgbaImage wide;
	wide.Width = 8;
	wide.Height = 4;
	wide.Pixels.assign(8 * 4 * 4, 255);

	const RgbaImage scaled = ScaleToFit(wide, 4);
	CHECK(scaled.Width == 4);
	CHECK(scaled.Height == 2);
	CHECK(scaled.Pixels.size() == 4 * 2 * 4);

	const RgbaImage untouched = ScaleToFit(wide, 16);
	CHECK(untouched.Width == 8);
	CHECK(untouched.Height == 4);
}

TEST_CASE("A truncated png is unreadable with a reason")
{
	const auto bytes = Bytes(ImageFixtures::kPng2x2);
	const std::span<const std::uint8_t> truncated = bytes.first(20);

	auto decoded = DecodeImage(truncated, ImageFormat::Png, DecodeLimits{});

	REQUIRE_FALSE(decoded.HasValue());
	CHECK(decoded.GetError().Code == ErrorCode::InvalidArgument);
	CHECK_FALSE(decoded.GetError().Message.empty());
}

TEST_CASE("Gif frames are counted without decoding them")
{
	CHECK(CountGifFrames(Bytes(ImageFixtures::kGif2Frames2x2)) == 2);
	CHECK(CountGifFrames(Bytes(ImageFixtures::kPng2x2)) == 0);
}
