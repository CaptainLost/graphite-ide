#pragma once

#include <vector>

namespace Core {

	struct BorderPoint
	{
		float X = 0.0f;
		float Y = 0.0f;
	};

	struct SplitBorder
	{
		std::vector<BorderPoint> Drained;
		std::vector<BorderPoint> Remaining;
	};

	// The first and last points are the same, so a closed polyline drawn from it must leave one out.
	std::vector<BorderPoint> TraceToastBorder(float left, float top, float right, float bottom,
											  float cornerRadius, int segmentsPerCorner);

	SplitBorder SplitToastBorder(const std::vector<BorderPoint>& border, float shareOfLifetimeLeft);
}
