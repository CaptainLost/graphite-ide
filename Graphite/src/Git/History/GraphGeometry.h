#pragma once

#include "Core/Git/GraphLayout.h"

#include <cstddef>

namespace Graphite {

	struct GraphMetrics
	{
		float LaneWidth = 0.0f;
		float RowHeight = 0.0f;
		float DotRadius = 0.0f;
	};

	float LaneCentreX(const GraphMetrics& metrics, std::size_t lane);

	float RowCentreY(const GraphMetrics& metrics, std::size_t row);

	float GraphWidth(const GraphMetrics& metrics, std::size_t laneCount);

	enum class EdgeShape
	{
		Straight,
		Fork,
		Merge
	};

	EdgeShape ShapeOf(const Core::GraphEdge& edge);
}
