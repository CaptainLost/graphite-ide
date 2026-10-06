#include "corepch.h"

#include "Core/ImGui/ToastBorder.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Core {

	namespace {
		constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2.0f;

		// ImGui gives a segment shorter than this a direction at random, and the joint after it
		// draws as a blob; the head of a split can land that close to a vertex.
		constexpr float kShortestSegmentToDraw = 0.01f;

		struct Corner
		{
			float CentreX = 0.0f;
			float CentreY = 0.0f;
			float StartAngle = 0.0f;
		};

		struct BorderPosition
		{
			std::size_t SegmentEndIndex = 0;
			BorderPoint Point;
		};

		float DistanceBetween(const BorderPoint& from, const BorderPoint& to)
		{
			return std::hypot(to.X - from.X, to.Y - from.Y);
		}

		void AppendUnlessRepeated(std::vector<BorderPoint>& points, const BorderPoint& point)
		{
			const bool repeatsLastPoint = !points.empty() && DistanceBetween(points.back(), point) < kShortestSegmentToDraw;
			if (!repeatsLastPoint)
			{
				points.push_back(point);
			}
		}

		void AppendCornerArc(std::vector<BorderPoint>& border, const Corner& corner, float radius, int arcSegments)
		{
			for (int arcPointIndex = 0; arcPointIndex <= arcSegments; ++arcPointIndex)
			{
				const float shareOfArc = arcSegments == 0 ? 0.0f : static_cast<float>(arcPointIndex) / static_cast<float>(arcSegments);
				const float angle = corner.StartAngle + kQuarterTurn * shareOfArc;
				AppendUnlessRepeated(border, {.X = corner.CentreX + radius * std::cos(angle), .Y = corner.CentreY + radius * std::sin(angle)});
			}
		}

		BorderPoint PointBetween(const BorderPoint& from, const BorderPoint& to, float shareOfTheWay)
		{
			return {.X = from.X + (to.X - from.X) * shareOfTheWay, .Y = from.Y + (to.Y - from.Y) * shareOfTheWay};
		}

		float LengthOf(const std::vector<BorderPoint>& border)
		{
			float length = 0.0f;
			for (std::size_t segmentEndIndex = 1; segmentEndIndex < border.size(); ++segmentEndIndex)
			{
				length += DistanceBetween(border[segmentEndIndex - 1], border[segmentEndIndex]);
			}
			return length;
		}

		BorderPosition FindPositionAlong(const std::vector<BorderPoint>& border, float distanceAlong)
		{
			float distanceWalked = 0.0f;
			for (std::size_t segmentEndIndex = 1; segmentEndIndex < border.size(); ++segmentEndIndex)
			{
				const BorderPoint& segmentStart = border[segmentEndIndex - 1];
				const BorderPoint& segmentEnd = border[segmentEndIndex];
				const float segmentLength = DistanceBetween(segmentStart, segmentEnd);

				const bool positionIsInThisSegment = segmentLength > 0.0f && distanceWalked + segmentLength >= distanceAlong;
				if (positionIsInThisSegment)
				{
					const float shareOfSegment = (distanceAlong - distanceWalked) / segmentLength;
					return {.SegmentEndIndex = segmentEndIndex, .Point = PointBetween(segmentStart, segmentEnd, shareOfSegment)};
				}

				distanceWalked += segmentLength;
			}

			return {.SegmentEndIndex = border.size() - 1, .Point = border.back()};
		}
	}

	std::vector<BorderPoint> TraceToastBorder(float left, float top, float right, float bottom,
											  float cornerRadius, int segmentsPerCorner)
	{
		const float largestRadius = std::max(std::min(right - left, bottom - top) / 2.0f, 0.0f);
		const float radius = std::clamp(cornerRadius, 0.0f, largestRadius);
		const int arcSegments = radius > 0.0f ? std::max(segmentsPerCorner, 1) : 0;

		const Corner topRight{.CentreX = right - radius, .CentreY = top + radius, .StartAngle = -kQuarterTurn};
		const Corner bottomRight{.CentreX = right - radius, .CentreY = bottom - radius, .StartAngle = 0.0f};
		const Corner bottomLeft{.CentreX = left + radius, .CentreY = bottom - radius, .StartAngle = kQuarterTurn};
		const Corner topLeft{.CentreX = left + radius, .CentreY = top + radius, .StartAngle = 2.0f * kQuarterTurn};

		const BorderPoint middleOfTop{.X = (left + right) / 2.0f, .Y = top};

		std::vector<BorderPoint> border;
		border.push_back(middleOfTop);
		for (const Corner& corner : {topRight, bottomRight, bottomLeft, topLeft})
		{
			AppendCornerArc(border, corner, radius, arcSegments);
		}
		AppendUnlessRepeated(border, middleOfTop);

		return border;
	}

	SplitBorder SplitToastBorder(const std::vector<BorderPoint>& border, float shareOfLifetimeLeft)
	{
		const float shareLeft = std::clamp(shareOfLifetimeLeft, 0.0f, 1.0f);
		const bool nothingDrained = shareLeft >= 1.0f || border.size() < 2;
		if (nothingDrained)
		{
			return {.Drained = {}, .Remaining = border};
		}

		const bool nothingRemains = shareLeft <= 0.0f;
		if (nothingRemains)
		{
			return {.Drained = border, .Remaining = {}};
		}

		const float drainedLength = LengthOf(border) * (1.0f - shareLeft);
		const BorderPosition head = FindPositionAlong(border, drainedLength);

		SplitBorder split;
		for (std::size_t pointIndex = 0; pointIndex < head.SegmentEndIndex; ++pointIndex)
		{
			split.Drained.push_back(border[pointIndex]);
		}
		AppendUnlessRepeated(split.Drained, head.Point);

		split.Remaining.push_back(split.Drained.back());
		for (std::size_t pointIndex = head.SegmentEndIndex; pointIndex < border.size(); ++pointIndex)
		{
			AppendUnlessRepeated(split.Remaining, border[pointIndex]);
		}

		return split;
	}
}
