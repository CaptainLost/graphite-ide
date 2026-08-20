#pragma once

#include "Core/Git/GraphLayout.h"

#include <cstddef>

namespace Graphite {

	// The sizes the graph column is drawn at. Kept apart from the widget so the arithmetic
	// that decides where a lane sits can be checked as numbers.
	struct GraphMetrics
	{
		float LaneWidth = 0.0f;
		float RowHeight = 0.0f;
		float DotRadius = 0.0f;
	};

	float LaneCentreX(const GraphMetrics& metrics, std::size_t lane);

	// Zero for an empty history. A linear history reports one lane, so there is no case
	// where a non-empty graph wants no width.
	float GraphWidth(const GraphMetrics& metrics, std::size_t laneCount);

	enum class EdgeShape
	{
		// Down the same lane.
		Straight,

		// Out to a lane on the right: a branch beginning, read from the bottom up.
		Fork,

		// In to a lane on the left: two lines joining.
		Merge
	};

	EdgeShape ShapeOf(const Core::GraphEdge& edge);
}
