#include <doctest/doctest.h>

#include "Core/ImGui/ToastBorder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace {
	constexpr float kLeft = 10.0f;
	constexpr float kTop = 20.0f;
	constexpr float kRight = 210.0f;
	constexpr float kBottom = 80.0f;
	constexpr float kMiddleOfTop = (kLeft + kRight) / 2.0f;
	constexpr int kSegmentsPerCorner = 16;
	constexpr float kCornerRadius = 10.0f;
	constexpr float kSquareCorners = 0.0f;
	constexpr float kRadiusWiderThanTheCard = 1000.0f;

	float LengthOf(const std::vector<Core::BorderPoint>& points)
	{
		float length = 0.0f;
		for (std::size_t pointIndex = 1; pointIndex < points.size(); ++pointIndex)
		{
			const float stepX = points[pointIndex].X - points[pointIndex - 1].X;
			const float stepY = points[pointIndex].Y - points[pointIndex - 1].Y;
			length += std::hypot(stepX, stepY);
		}
		return length;
	}

	float ShortestSegmentOf(const std::vector<Core::BorderPoint>& points)
	{
		float shortest = std::numeric_limits<float>::max();
		for (std::size_t pointIndex = 1; pointIndex < points.size(); ++pointIndex)
		{
			const float stepX = points[pointIndex].X - points[pointIndex - 1].X;
			const float stepY = points[pointIndex].Y - points[pointIndex - 1].Y;
			shortest = std::min(shortest, std::hypot(stepX, stepY));
		}
		return shortest;
	}

	bool SamePoint(const Core::BorderPoint& first, const Core::BorderPoint& second)
	{
		return first.X == doctest::Approx(second.X) && first.Y == doctest::Approx(second.Y);
	}

	std::vector<Core::BorderPoint> RoundedBorder()
	{
		return Core::TraceToastBorder(kLeft, kTop, kRight, kBottom, kCornerRadius, kSegmentsPerCorner);
	}
}

TEST_CASE("A toast's border starts and ends at the middle of its top edge")
{
	const std::vector<Core::BorderPoint> border = RoundedBorder();

	REQUIRE(border.size() > 2);
	CHECK(SamePoint(border.front(), {kMiddleOfTop, kTop}));
	CHECK(SamePoint(border.back(), {kMiddleOfTop, kTop}));
}

TEST_CASE("A toast's border runs clockwise, rightwards along the top first")
{
	const std::vector<Core::BorderPoint> border = RoundedBorder();

	REQUIRE(border.size() > 2);
	CHECK(border[1].X > border[0].X);
	CHECK(border[1].Y == doctest::Approx(kTop));
	CHECK(border[border.size() - 2].X < border.back().X);
}

TEST_CASE("A toast's border touches every side of the card and never leaves it")
{
	const std::vector<Core::BorderPoint> border = RoundedBorder();
	REQUIRE_FALSE(border.empty());

	float leftmost = border.front().X;
	float rightmost = border.front().X;
	float topmost = border.front().Y;
	float bottommost = border.front().Y;
	for (const Core::BorderPoint& point : border)
	{
		leftmost = std::min(leftmost, point.X);
		rightmost = std::max(rightmost, point.X);
		topmost = std::min(topmost, point.Y);
		bottommost = std::max(bottommost, point.Y);
	}

	CHECK(leftmost == doctest::Approx(kLeft));
	CHECK(rightmost == doctest::Approx(kRight));
	CHECK(topmost == doctest::Approx(kTop));
	CHECK(bottommost == doctest::Approx(kBottom));
}

TEST_CASE("A square-cornered toast border is as long as the card's perimeter")
{
	const std::vector<Core::BorderPoint> border =
		Core::TraceToastBorder(kLeft, kTop, kRight, kBottom, kSquareCorners, kSegmentsPerCorner);

	CHECK(LengthOf(border) == doctest::Approx(2.0f * ((kRight - kLeft) + (kBottom - kTop))));
}

TEST_CASE("A rounded toast border is as long as its straight sides and four quarter circles")
{
	const float straightSides = 2.0f * ((kRight - kLeft) + (kBottom - kTop)) - 8.0f * kCornerRadius;
	const float quarterCircles = 2.0f * std::numbers::pi_v<float> * kCornerRadius;

	CHECK(LengthOf(RoundedBorder()) == doctest::Approx(straightSides + quarterCircles).epsilon(0.001));
}

TEST_CASE("A corner radius wider than half the card's height is cut to half of it")
{
	const std::vector<Core::BorderPoint> border =
		Core::TraceToastBorder(kLeft, kTop, kRight, kBottom, kRadiusWiderThanTheCard, kSegmentsPerCorner);

	constexpr float halfHeight = (kBottom - kTop) / 2.0f;
	const float straightSides = 2.0f * ((kRight - kLeft) + (kBottom - kTop)) - 8.0f * halfHeight;
	const float quarterCircles = 2.0f * std::numbers::pi_v<float> * halfHeight;

	CHECK(LengthOf(border) == doctest::Approx(straightSides + quarterCircles).epsilon(0.001));
}

TEST_CASE("Splitting a toast's border leaves the share of its length that the lifetime has left")
{
	const std::vector<Core::BorderPoint> border = RoundedBorder();
	const float wholeLength = LengthOf(border);

	for (const float shareLeft : {0.1f, 0.25f, 0.5f, 0.8f, 0.97f})
	{
		CAPTURE(shareLeft);
		const Core::SplitBorder split = Core::SplitToastBorder(border, shareLeft);

		CHECK(LengthOf(split.Remaining) == doctest::Approx(wholeLength * shareLeft).epsilon(0.0001));
		CHECK(LengthOf(split.Drained) == doctest::Approx(wholeLength * (1.0f - shareLeft)).epsilon(0.0001));
	}
}

TEST_CASE("The drained part runs from the top's middle to where the remaining part takes over")
{
	const std::vector<Core::BorderPoint> border = RoundedBorder();

	for (const float shareLeft : {0.02f, 0.3f, 0.6f, 0.95f, 0.999f})
	{
		CAPTURE(shareLeft);
		const Core::SplitBorder split = Core::SplitToastBorder(border, shareLeft);

		REQUIRE(split.Drained.size() >= 2);
		REQUIRE(split.Remaining.size() >= 2);
		CHECK(SamePoint(split.Drained.front(), border.front()));
		CHECK(SamePoint(split.Drained.back(), split.Remaining.front()));
		CHECK(SamePoint(split.Remaining.back(), border.back()));
	}
}

TEST_CASE("A split on a corner, or a hair from one, puts the head on the corner and repeats no point")
{
	const std::vector<Core::BorderPoint> border =
		Core::TraceToastBorder(kLeft, kTop, kRight, kBottom, kSquareCorners, kSegmentsPerCorner);
	const float wholeLength = LengthOf(border);
	const float middleToTopRight = kRight - kMiddleOfTop;
	const float shareLeftAtTopRight = (wholeLength - middleToTopRight) / wholeLength;

	for (const float shareLeft : {shareLeftAtTopRight, shareLeftAtTopRight + 1e-7f, shareLeftAtTopRight - 1e-7f})
	{
		CAPTURE(shareLeft);
		const Core::SplitBorder split = Core::SplitToastBorder(border, shareLeft);

		REQUIRE_FALSE(split.Remaining.empty());
		CHECK(SamePoint(split.Remaining.front(), {kRight, kTop}));
		for (const std::vector<Core::BorderPoint>* part : {&split.Drained, &split.Remaining})
		{
			CHECK(ShortestSegmentOf(*part) >= 0.01f);
		}
	}
}

TEST_CASE("With the whole lifetime left nothing of a toast's border is drained")
{
	const std::vector<Core::BorderPoint> border = RoundedBorder();
	const Core::SplitBorder split = Core::SplitToastBorder(border, 1.0f);

	CHECK(split.Drained.empty());
	REQUIRE(split.Remaining.size() == border.size());
	CHECK(LengthOf(split.Remaining) == doctest::Approx(LengthOf(border)));
}

TEST_CASE("With no lifetime left the whole of a toast's border is drained")
{
	const std::vector<Core::BorderPoint> border = RoundedBorder();
	const Core::SplitBorder split = Core::SplitToastBorder(border, 0.0f);

	CHECK(split.Remaining.empty());
	CHECK(LengthOf(split.Drained) == doctest::Approx(LengthOf(border)));
}

TEST_CASE("A share of lifetime outside zero to one splits a toast's border as its nearest end")
{
	const std::vector<Core::BorderPoint> border = RoundedBorder();

	CHECK(Core::SplitToastBorder(border, 1.5f).Drained.empty());
	CHECK(Core::SplitToastBorder(border, -0.5f).Remaining.empty());
}
