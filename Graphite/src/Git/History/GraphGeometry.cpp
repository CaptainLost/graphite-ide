#include "Git/History/GraphGeometry.h"

namespace Graphite {

	float LaneCentreX(const GraphMetrics& metrics, std::size_t lane)
	{
		return (static_cast<float>(lane) + 0.5f) * metrics.LaneWidth;
	}

	float RowCentreY(const GraphMetrics& metrics, std::size_t row)
	{
		return (static_cast<float>(row) + 0.5f) * metrics.RowHeight;
	}

	float GraphWidth(const GraphMetrics& metrics, std::size_t laneCount)
	{
		return static_cast<float>(laneCount) * metrics.LaneWidth;
	}

	EdgeShape ShapeOf(const Core::GraphEdge& edge)
	{
		if (edge.ToLane == edge.FromLane)
		{
			return EdgeShape::Straight;
		}

		return edge.ToLane > edge.FromLane ? EdgeShape::Fork : EdgeShape::Merge;
	}
}
