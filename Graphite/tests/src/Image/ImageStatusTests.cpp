#include <doctest/doctest.h>

#include "Image/ImageStatus.h"

namespace {

	Graphite::ImageFacts Facts(Core::ImageFormat format, int width, int height, std::uint64_t size)
	{
		Graphite::ImageFacts facts;
		facts.Format = format;
		facts.Width = width;
		facts.Height = height;
		facts.FileSize = size;
		return facts;
	}

} // namespace

TEST_CASE("An image's facts read size, format and file size")
{
	const auto items = Graphite::ImageStatusItems(Facts(Core::ImageFormat::Png, 256, 256, 25190), std::nullopt);

	REQUIRE(items.size() == 1);
	CHECK(items[0].Text == "256 × 256 · PNG · 24.6 KB");
	CHECK_FALSE(items[0].Code);
	CHECK_FALSE(items[0].Swatch.has_value());
}

TEST_CASE("An ico names how many sizes it holds")
{
	auto facts = Facts(Core::ImageFormat::Ico, 16, 16, 1126);
	facts.EntryCount = 6;

	const auto items = Graphite::ImageStatusItems(facts, std::nullopt);

	REQUIRE(items.size() == 1);
	CHECK(items[0].Text == "16 × 16 · ICO · 6 sizes · 1.1 KB");
}

TEST_CASE("An animated gif names its frames")
{
	auto facts = Facts(Core::ImageFormat::Gif, 64, 48, 25190);
	facts.FileFrameCount = 12;
	facts.ShownFrameCount = 12;

	const auto items = Graphite::ImageStatusItems(facts, std::nullopt);

	REQUIRE(items.size() == 1);
	CHECK(items[0].Text == "64 × 48 · GIF · 12 frames · 24.6 KB");
}

TEST_CASE("A gif cut to its first frame says so")
{
	auto facts = Facts(Core::ImageFormat::Gif, 64, 48, 25190);
	facts.FileFrameCount = 40;
	facts.ShownFrameCount = 1;

	const auto items = Graphite::ImageStatusItems(facts, std::nullopt);

	REQUIRE(items.size() == 1);
	CHECK(items[0].Text == "64 × 48 · GIF · first of 40 frames · 24.6 KB");
}

TEST_CASE("One frame or one entry adds nothing to the facts")
{
	const auto items = Graphite::ImageStatusItems(Facts(Core::ImageFormat::Gif, 8, 8, 25190), std::nullopt);

	REQUIRE(items.size() == 1);
	CHECK(items[0].Text == "8 × 8 · GIF · 24.6 KB");
}

TEST_CASE("A hovered pixel comes first, as its place and its colour")
{
	Graphite::HoveredPixel pixel;
	pixel.At = {128, 126};
	pixel.R = 230;
	pixel.G = 237;
	pixel.B = 246;
	pixel.A = 255;

	const auto items = Graphite::ImageStatusItems(Facts(Core::ImageFormat::Png, 256, 256, 25190), pixel);

	REQUIRE(items.size() == 3);
	CHECK(items[0] == Graphite::ViewStatusItem{"128, 126", false, IM_COL32(230, 237, 246, 255)});
	CHECK(items[1] == Graphite::ViewStatusItem{"#E6EDF6FF", true, std::nullopt});
	CHECK(items[2].Text == "256 × 256 · PNG · 24.6 KB");
}
