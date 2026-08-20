#include <doctest/doctest.h>

#include "Core/Git/GraphLayout.h"

#include <algorithm>
#include <string>
#include <vector>

using Core::CommitGraph;
using Core::CommitSummary;
using Core::Oid;

namespace {

	// Ids that read in a test: the first byte is the number, the rest is zero.
	Oid Id(std::uint8_t value)
	{
		Oid id;
		id.Bytes[0] = value;
		return id;
	}

	CommitSummary Commit(std::uint8_t id, std::vector<std::uint8_t> parents)
	{
		CommitSummary commit;
		commit.Id = Id(id);

		for (std::uint8_t parent : parents)
		{
			commit.Parents.push_back(Id(parent));
		}

		return commit;
	}

	std::size_t LaneOf(const CommitGraph& graph, std::uint8_t id)
	{
		const auto node = std::find_if(graph.Nodes.begin(), graph.Nodes.end(),
									   [id](const Core::GraphNode& candidate) {
										   return candidate.Id == Id(id);
									   });

		REQUIRE(node != graph.Nodes.end());
		return node->Lane;
	}
}

TEST_CASE("An empty history lays out to nothing")
{
	const CommitGraph graph = Core::LayoutCommits({});

	CHECK(graph.Nodes.empty());
	CHECK(graph.Edges.empty());
	CHECK(graph.LaneCount == 0);
}

TEST_CASE("A linear history occupies one lane")
{
	const CommitGraph graph = Core::LayoutCommits({Commit(3, {2}), Commit(2, {1}), Commit(1, {})});

	REQUIRE(graph.Nodes.size() == 3);
	CHECK(graph.LaneCount == 1);
	CHECK(LaneOf(graph, 3) == 0);
	CHECK(LaneOf(graph, 2) == 0);
	CHECK(LaneOf(graph, 1) == 0);

	CHECK(graph.Nodes[0].Row == 0);
	CHECK(graph.Nodes[2].Row == 2);
	CHECK(graph.Edges.size() == 2);
}

TEST_CASE("A merge keeps its first parent on its own lane and opens a second")
{
	//   4  merge of 3 and 2
	//   |\
	//   3 |
	//   | 2
	//   |/
	//   1
	const CommitGraph graph = Core::LayoutCommits(
		{Commit(4, {3, 2}), Commit(3, {1}), Commit(2, {1}), Commit(1, {})});

	CHECK(graph.LaneCount == 2);
	CHECK(LaneOf(graph, 4) == 0);

	// The first parent inherits the merge's lane; the second is pushed sideways.
	CHECK(LaneOf(graph, 3) == 0);
	CHECK(LaneOf(graph, 2) == 1);
	CHECK(LaneOf(graph, 1) == 0);
}

TEST_CASE("A lane is reused once the branch on it has been drawn")
{
	// 2 and 3 are unrelated roots. Once 3's line ends, the lane it held is free again.
	const CommitGraph graph =
		Core::LayoutCommits({Commit(4, {3, 2}), Commit(3, {}), Commit(2, {}), Commit(1, {})});

	// Lane 1 carried commit 2; commit 1, an unrelated root below it, may take that lane
	// back rather than opening a third.
	CHECK(graph.LaneCount <= 2);
}

TEST_CASE("An edge to a parent outside the loaded page is dropped")
{
	// Commit 1's parent was not loaded — the page ended. Drawing an edge to a row that
	// does not exist would index past the end of the node list.
	const CommitGraph graph = Core::LayoutCommits({Commit(2, {1}), Commit(1, {99})});

	CHECK(graph.Nodes.size() == 2);
	CHECK(graph.Edges.size() == 1);
	CHECK(graph.Edges.front().FromRow == 0);
	CHECK(graph.Edges.front().ToRow == 1);
}

TEST_CASE("Every edge names rows that exist")
{
	const CommitGraph graph = Core::LayoutCommits(
		{Commit(5, {4, 3}), Commit(4, {2}), Commit(3, {2}), Commit(2, {1}), Commit(1, {})});

	for (const Core::GraphEdge& edge : graph.Edges)
	{
		CHECK(edge.FromRow < graph.Nodes.size());
		CHECK(edge.ToRow < graph.Nodes.size());
		CHECK(edge.FromRow < edge.ToRow);
		CHECK(edge.FromLane < graph.LaneCount);
		CHECK(edge.ToLane < graph.LaneCount);
	}
}
