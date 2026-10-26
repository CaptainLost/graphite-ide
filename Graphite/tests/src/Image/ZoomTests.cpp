#include <doctest/doctest.h>

#include "Image/Zoom.h"

namespace {

	const ImVec2 kAreaSize(764.0f, 508.0f);
	const ImRect kArea(ImVec2(100.0f, 50.0f), ImVec2(864.0f, 558.0f));

} // namespace

TEST_CASE("Zoom steps go up and down the fixed list")
{
	CHECK(Graphite::StepIn(1.0f) == doctest::Approx(1.5f));
	CHECK(Graphite::StepOut(1.0f) == doctest::Approx(2.0f / 3.0f));
	CHECK(Graphite::StepIn(32.0f) == 32.0f);
}

TEST_CASE("A zoom between steps moves to the next step")
{
	CHECK(Graphite::StepIn(0.9f) == doctest::Approx(1.0f));
	CHECK(Graphite::StepOut(0.9f) == doctest::Approx(2.0f / 3.0f));
}

TEST_CASE("Zooming out below the smallest step does nothing, zooming in reaches it")
{
	CHECK(Graphite::StepOut(0.07f) == 0.07f);
	CHECK(Graphite::StepIn(0.07f) == doctest::Approx(0.1f));
}

TEST_CASE("A picture that fits opens at 100%")
{
	CHECK(Graphite::OpeningZoom(ImVec2(256.0f, 256.0f), kAreaSize) == 1.0f);
}

TEST_CASE("A picture that does not fit opens at Fit")
{
	CHECK(Graphite::OpeningZoom(ImVec2(2000.0f, 1000.0f), kAreaSize) == doctest::Approx((764.0f - 48.0f) / 2000.0f));
}

TEST_CASE("Fit enlarges a small picture")
{
	CHECK(Graphite::FitZoom(ImVec2(256.0f, 256.0f), kAreaSize) == doctest::Approx((508.0f - 48.0f) / 256.0f));
}

TEST_CASE("A long thin picture fits below the smallest step")
{
	CHECK(Graphite::FitZoom(ImVec2(10000.0f, 40.0f), kAreaSize) < 0.1f);
}

TEST_CASE("Zooming around a point keeps that point over the same pixel")
{
	const ImVec2 picture(400.0f, 300.0f);
	const Graphite::ZoomState before{1.0f, ImVec2(10.0f, -5.0f), true};
	const ImVec2 point(430.5f, 260.5f);

	const auto pixelBefore = Graphite::PixelUnder(point, before, picture, kArea);
	REQUIRE(pixelBefore.has_value());

	for (const float zoom : {2.0f, 4.0f, 8.0f}) {
		const Graphite::ZoomState after = Graphite::ZoomAround(before, zoom, point, picture, kArea);
		CHECK(after.Zoom == zoom);
		CHECK(Graphite::PixelUnder(point, after, picture, kArea) == pixelBefore);
	}
}

TEST_CASE("Panning keeps 32 pixels of the picture in view")
{
	const ImVec2 picture(400.0f, 300.0f);
	const Graphite::ZoomState far{1.0f, ImVec2(5000.0f, -5000.0f), true};

	const Graphite::ZoomState clamped = Graphite::ClampPan(far, picture, kArea);
	const ImRect placed = Graphite::PlacePicture(clamped, picture, kArea);

	CHECK(placed.Min.x == doctest::Approx(kArea.Max.x - Graphite::kPanKeepVisible));
	CHECK(placed.Max.y == doctest::Approx(kArea.Min.y + Graphite::kPanKeepVisible));

	const Graphite::ZoomState centred{1.0f, ImVec2(3.0f, 4.0f), true};
	CHECK(Graphite::ClampPan(centred, picture, kArea).Pan.x == 3.0f);
}

TEST_CASE("The pixel under the pointer is found in file pixels")
{
	const ImVec2 picture(100.0f, 50.0f);
	const Graphite::ZoomState state{4.0f, ImVec2(0.0f, 0.0f), true};
	const ImRect placed = Graphite::PlacePicture(state, picture, kArea);

	const auto inside = Graphite::PixelUnder(ImVec2(placed.Min.x + 9.0f, placed.Min.y + 5.0f), state, picture, kArea);
	REQUIRE(inside.has_value());
	CHECK(*inside == Graphite::PixelPoint{2, 1});

	CHECK_FALSE(Graphite::PixelUnder(ImVec2(placed.Min.x - 1.0f, placed.Min.y + 5.0f), state, picture, kArea).has_value());
	CHECK_FALSE(Graphite::PixelUnder(ImVec2(placed.Max.x, placed.Min.y + 5.0f), state, picture, kArea).has_value());
}

TEST_CASE("Zoom labels read as whole or half percentages")
{
	CHECK(Graphite::ZoomLabel(1.0f) == "100%");
	CHECK(Graphite::ZoomLabel(0.125f) == "12.5%");
	CHECK(Graphite::ZoomLabel(1.0f / 3.0f) == "33%");
	CHECK(Graphite::ZoomLabel(2.0f / 3.0f) == "67%");
	CHECK(Graphite::ZoomLabel(16.0f) == "1600%");
}
