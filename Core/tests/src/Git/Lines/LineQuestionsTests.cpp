#include <doctest/doctest.h>

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineQuestions.h"

#include "Git/Lines/GraphBuilder.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

	using Index = Core::AncestryGraph::Index;

	Core::LineDistance Distance(const GraphBuilder& builder, std::string_view from, std::string_view to)
	{
		const Core::AncestryGraph graph = builder.Graph();
		return Core::MeasureDistance(graph, graph.Find(OidOf(from)), graph.Find(OidOf(to)));
	}

	std::vector<bool> Holding(const GraphBuilder& builder, std::string_view commit, const std::vector<std::string_view>& tipNames)
	{
		const Core::AncestryGraph graph = builder.Graph();
		std::vector<Index> tips;

		for (const std::string_view name : tipNames)
		{
			tips.push_back(graph.Find(OidOf(name)));
		}

		return Core::TipsHolding(graph, graph.Find(OidOf(commit)), tips);
	}
}

TEST_CASE("Lines: distance along one line")
{
	GraphBuilder builder;
	builder.Chain("c", 5, std::nullopt, 0, {});

	const Core::LineDistance ahead = Distance(builder, "c5", "c2");
	CHECK(ahead.Ahead == 3);
	CHECK(ahead.Behind == 0);
	CHECK(ahead.ForkTime == GraphBuilder::Seconds(1));
	CHECK(ahead.ForkOnLine);

	const Core::LineDistance behind = Distance(builder, "c2", "c5");
	CHECK(behind.Ahead == 0);
	CHECK(behind.Behind == 3);
	CHECK(behind.ForkTime == GraphBuilder::Seconds(1));
	CHECK(behind.ForkOnLine);

	const Core::LineDistance same = Distance(builder, "c3", "c3");
	CHECK(same == Core::LineDistance{.Ahead = 0, .Behind = 0, .ForkTime = GraphBuilder::Seconds(2), .ForkOnLine = true});
}

TEST_CASE("Lines: distance across a merge counts the merged commits")
{
	GraphBuilder builder;
	builder.Commit("m1", {}, 0);
	builder.Commit("m2", {"m1"}, 2);
	builder.Commit("m3", {"m2"}, 3);
	builder.Commit("f1", {"m1"}, 1);
	builder.Commit("f2", {"f1"}, 4);
	builder.Commit("m4", {"m3", "f2"}, 5);

	const Core::LineDistance distance = Distance(builder, "m4", "m1");
	CHECK(distance.Ahead == 5);
	CHECK(distance.Behind == 0);
	CHECK(distance.ForkTime == GraphBuilder::Seconds(0));
	CHECK(distance.ForkOnLine);

	const Core::LineDistance sideways = Distance(builder, "m3", "f2");
	CHECK(sideways.Ahead == 2);
	CHECK(sideways.Behind == 2);
	CHECK(sideways.ForkTime == GraphBuilder::Seconds(0));
}

TEST_CASE("Lines: unrelated histories have no fork")
{
	GraphBuilder builder;
	builder.Chain("a", 3, std::nullopt, 0, {});
	builder.Chain("b", 2, std::nullopt, 0, {});

	const Core::LineDistance distance = Distance(builder, "a3", "b2");
	CHECK(distance.Ahead == 3);
	CHECK(distance.Behind == 2);
	CHECK_FALSE(distance.ForkTime.has_value());
	CHECK_FALSE(distance.ForkOnLine);
}

TEST_CASE("Lines: a fork reached through a merge is not on the line")
{
	GraphBuilder builder;
	builder.Commit("m1", {}, 0);
	builder.Commit("f1", {"m1"}, 1);
	builder.Commit("m2", {"m1"}, 2);
	builder.Commit("m3", {"m2", "f1"}, 3);
	builder.Commit("g", {"f1"}, 4);

	const Core::LineDistance distance = Distance(builder, "g", "m3");
	CHECK(distance.Ahead == 1);
	CHECK(distance.Behind == 2);
	CHECK(distance.ForkTime == GraphBuilder::Seconds(1));
	CHECK_FALSE(distance.ForkOnLine);
}

TEST_CASE("Lines: tips holding a commit merged in through a second parent")
{
	GraphBuilder builder;
	builder.Commit("m1", {}, 0);
	builder.Commit("f1", {"m1"}, 1);
	builder.Commit("m2", {"m1"}, 2);
	builder.Commit("m3", {"m2", "f1"}, 3);
	builder.Commit("g", {"f1"}, 4);
	builder.Commit("other", {"m1"}, 5);

	const std::vector<bool> holding = Holding(builder, "f1", {"m3", "m2", "g", "f1", "other"});
	REQUIRE(holding.size() == 5);
	CHECK(holding[0]);
	CHECK_FALSE(holding[1]);
	CHECK(holding[2]);
	CHECK(holding[3]);
	CHECK_FALSE(holding[4]);

	CHECK(Holding(builder, "m1", {}).empty());
}

TEST_CASE("Lines: distance on a chain of two hundred thousand commits does not recurse")
{
	constexpr int kCommits = 200000;
	GraphBuilder builder;
	builder.Chain("c", kCommits, std::nullopt, 0, {});

	const Core::LineDistance distance = Distance(builder, "c200000", "c1");
	CHECK(distance.Ahead == static_cast<std::size_t>(kCommits - 1));
	CHECK(distance.Behind == 0);
	CHECK(distance.ForkTime == GraphBuilder::Seconds(0));
	CHECK(distance.ForkOnLine);

	const std::vector<bool> holding = Holding(builder, "c1", {"c200000", "c1"});
	CHECK(holding[0]);
	CHECK(holding[1]);
}

TEST_CASE("Lines: a child older than its parent still counts right")
{
	GraphBuilder builder;
	builder.Commit("r", {}, 10);
	builder.Commit("c", {"r"}, 5);
	builder.Commit("d", {"c"}, 20);
	builder.Commit("x", {"r"}, 11);

	const Core::LineDistance distance = Distance(builder, "d", "x");
	CHECK(distance.Ahead == 2);
	CHECK(distance.Behind == 1);
	CHECK(distance.ForkTime == GraphBuilder::Seconds(10));
	CHECK(distance.ForkOnLine);

	const std::vector<bool> holding = Holding(builder, "r", {"d", "x"});
	CHECK(holding[0]);
	CHECK(holding[1]);
}

TEST_CASE("Lines: a branch merged through a second parent on a long line is not on the line")
{
	constexpr int kLineLength = 50000;
	GraphBuilder builder;
	builder.Chain("c", kLineLength, std::nullopt, 0, {});
	builder.Commit("side1", {"c10"}, kLineLength + 1);
	builder.Commit("merge", {"c50000", "side1"}, kLineLength + 2);

	// `side1` is an ancestor of `merge` but off its first-parent chain: the answer is false, and the check
	// ends at the generation of `side1` instead of following the chain down to its root.
	const Core::LineDistance distance = Distance(builder, "side1", "merge");
	CHECK(distance.Ahead == 0);
	CHECK(distance.Behind == static_cast<std::size_t>(kLineLength - 10 + 1));
	CHECK(distance.ForkTime == GraphBuilder::Seconds(kLineLength + 1));
	CHECK_FALSE(distance.ForkOnLine);
}
