#include <doctest/doctest.h>

#include "Git/History/GraphGeometry.h"

using Graphite::EdgeShape;
using Graphite::GraphMetrics;

namespace {

	GraphMetrics Metrics()
	{
		GraphMetrics metrics;
		metrics.LaneWidth = 14.0f;
		metrics.RowHeight = 18.0f;
		metrics.DotRadius = 4.0f;
		return metrics;
	}

	Core::GraphEdge Edge(std::size_t fromRow, std::size_t fromLane, std::size_t toRow,
						 std::size_t toLane)
	{
		Core::GraphEdge edge;
		edge.FromRow = fromRow;
		edge.FromLane = fromLane;
		edge.ToRow = toRow;
		edge.ToLane = toLane;
		return edge;
	}
}

TEST_CASE("A lane's centre sits half a lane in from its left edge")
{
	const GraphMetrics metrics = Metrics();

	CHECK(Graphite::LaneCentreX(metrics, 0) == doctest::Approx(7.0f));
	CHECK(Graphite::LaneCentreX(metrics, 1) == doctest::Approx(21.0f));
}

TEST_CASE("A row's centre sits half a row down from its top edge")
{
	const GraphMetrics metrics = Metrics();

	CHECK(Graphite::RowCentreY(metrics, 0) == doctest::Approx(9.0f));
	CHECK(Graphite::RowCentreY(metrics, 1) == doctest::Approx(27.0f));
}

TEST_CASE("The graph column is as wide as its lanes")
{
	const GraphMetrics metrics = Metrics();

	CHECK(Graphite::GraphWidth(metrics, 3) == doctest::Approx(42.0f));
}

TEST_CASE("A graph with no lanes, an empty history, takes no width and no minimum stripe")
{
	CHECK(Graphite::GraphWidth(Metrics(), 0) == doctest::Approx(0.0f));
}

TEST_CASE("An edge that stays in its lane is straight")
{
	CHECK(Graphite::ShapeOf(Edge(0, 1, 1, 1)) == EdgeShape::Straight);
}

TEST_CASE("An edge moving to a lane on the right is a fork, a branch starting seen from the bottom up")
{
	CHECK(Graphite::ShapeOf(Edge(0, 0, 3, 2)) == EdgeShape::Fork);
}

TEST_CASE("An edge moving to a lane on the left is a merge, two lines joining")
{
	CHECK(Graphite::ShapeOf(Edge(0, 2, 3, 0)) == EdgeShape::Merge);
}
