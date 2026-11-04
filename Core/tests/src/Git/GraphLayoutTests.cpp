#include <doctest/doctest.h>

#include "Core/Git/GraphLayout.h"

#include <string>
#include <vector>

using Core::CommitGraph;
using Core::CommitSummary;
using Core::Oid;

namespace {

	Oid IdNumbered(std::uint8_t number)
	{
		Oid id;
		id.Bytes[0] = number;
		return id;
	}

	CommitSummary Commit(std::uint8_t number, std::vector<std::uint8_t> parentNumbers)
	{
		CommitSummary commit;
		commit.Id = IdNumbered(number);

		for (const std::uint8_t parentNumber : parentNumbers)
		{
			commit.Parents.push_back(IdNumbered(parentNumber));
		}

		return commit;
	}

	CommitSummary RootCommit(std::uint8_t number)
	{
		const std::vector<std::uint8_t> noParents;
		return Commit(number, noParents);
	}

	const Core::GraphNode* FindNode(const CommitGraph& graph, std::uint8_t number)
	{
		for (const Core::GraphNode& candidate : graph.Nodes)
		{
			if (candidate.Id == IdNumbered(number))
			{
				return &candidate;
			}
		}

		return nullptr;
	}

	std::size_t LaneOf(const CommitGraph& graph, std::uint8_t number)
	{
		const Core::GraphNode* node = FindNode(graph, number);

		REQUIRE(node != nullptr);
		return node->Lane;
	}
}

TEST_CASE("An empty history lays out to nothing")
{
	const std::vector<CommitSummary> noCommits;
	const CommitGraph graph = Core::LayoutCommits(noCommits);

	CHECK(graph.Nodes.empty());
	CHECK(graph.Edges.empty());
	CHECK(graph.LaneCount == 0);
}

TEST_CASE("A linear history occupies one lane")
{
	const CommitGraph graph = Core::LayoutCommits({Commit(3, {2}), Commit(2, {1}), RootCommit(1)});

	REQUIRE(graph.Nodes.size() == 3);
	CHECK(graph.LaneCount == 1);
	CHECK(LaneOf(graph, 3) == 0);
	CHECK(LaneOf(graph, 2) == 0);
	CHECK(LaneOf(graph, 1) == 0);

	CHECK(graph.Nodes[0].Row == 0);
	CHECK(graph.Nodes[2].Row == 2);
	CHECK(graph.Edges.size() == 2);
}

TEST_CASE("A merge keeps its first parent on its own lane and pushes the second sideways")
{
	const CommitGraph graph = Core::LayoutCommits(
		{Commit(4, {3, 2}), Commit(3, {1}), Commit(2, {1}), RootCommit(1)});

	CHECK(graph.LaneCount == 2);
	CHECK(LaneOf(graph, 4) == 0);
	CHECK(LaneOf(graph, 3) == 0);
	CHECK(LaneOf(graph, 2) == 1);
	CHECK(LaneOf(graph, 1) == 0);
}

TEST_CASE("A lane is reused once the branch on it has been drawn, the leftmost free lane first")
{
	const CommitGraph graph =
		Core::LayoutCommits({Commit(4, {3, 2}), RootCommit(3), RootCommit(2), RootCommit(1)});

	CHECK(graph.LaneCount == 2);
	CHECK(LaneOf(graph, 1) == 0);
}

TEST_CASE("An edge to a parent outside the loaded page is dropped")
{
	const CommitGraph graph = Core::LayoutCommits({Commit(2, {1}), Commit(1, {99})});

	CHECK(graph.Nodes.size() == 2);
	CHECK(graph.Edges.size() == 1);
	CHECK(graph.Edges.front().FromRow == 0);
	CHECK(graph.Edges.front().ToRow == 1);
}

TEST_CASE("A parent outside the loaded page reserves no lane, even for a merge")
{
	const CommitGraph graph = Core::LayoutCommits({Commit(2, {1}), Commit(1, {98, 99})});

	CHECK(graph.LaneCount == 1);
	CHECK(LaneOf(graph, 2) == 0);
	CHECK(LaneOf(graph, 1) == 0);
	CHECK(graph.Edges.size() == 1);
	CHECK(graph.Edges.front().FromRow == 0);
	CHECK(graph.Edges.front().ToRow == 1);
}

TEST_CASE("A filtered history whose parents are all off-page occupies one lane")
{
	const CommitGraph graph = Core::LayoutCommits(
		{Commit(3, {100}), Commit(2, {101}), Commit(1, {102})});

	REQUIRE(graph.Nodes.size() == 3);
	CHECK(graph.LaneCount == 1);
	CHECK(LaneOf(graph, 3) == 0);
	CHECK(LaneOf(graph, 2) == 0);
	CHECK(LaneOf(graph, 1) == 0);
	CHECK(graph.Edges.empty());
}

TEST_CASE("Every edge names rows that exist")
{
	const CommitGraph graph = Core::LayoutCommits(
		{Commit(5, {4, 3}), Commit(4, {2}), Commit(3, {2}), Commit(2, {1}), RootCommit(1)});

	REQUIRE(graph.Edges.size() == 5);

	for (const Core::GraphEdge& edge : graph.Edges)
	{
		CHECK(edge.FromRow < graph.Nodes.size());
		CHECK(edge.ToRow < graph.Nodes.size());
		CHECK(edge.FromRow < edge.ToRow);
		CHECK(edge.FromLane < graph.LaneCount);
		CHECK(edge.ToLane < graph.LaneCount);
	}
}

TEST_CASE("Git: a first-parent layout gives a merge one edge")
{
	const std::vector<CommitSummary> commits = {Commit(4, {3, 2}), Commit(3, {1}), Commit(2, {1}), RootCommit(1)};

	const CommitGraph graph = Core::LayoutCommits(commits, Core::LayoutParents::FirstOnly);

	std::size_t edgesFromMerge = 0;

	for (const Core::GraphEdge& edge : graph.Edges)
	{
		if (edge.FromRow == 0)
		{
			++edgesFromMerge;
			CHECK(edge.ToRow == 1);
		}
	}

	CHECK(edgesFromMerge == 1);
	CHECK(LaneOf(graph, 4) == 0);
	CHECK(LaneOf(graph, 3) == 0);
}
