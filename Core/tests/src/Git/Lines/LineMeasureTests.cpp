#include <doctest/doctest.h>

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineMeasure.h"
#include "Core/Git/Lines/LineSplit.h"
#include "Core/Git/Lines/LineThresholds.h"

#include "Git/Lines/GraphBuilder.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

	using Index = Core::AncestryGraph::Index;

	// Splits at the named tips and measures the line the named commit is on.
	Core::LineQualities Measure(const GraphBuilder& builder, const std::vector<std::string_view>& tipNames,
								const std::vector<std::string_view>& baseNames, std::string_view onLine)
	{
		const Core::AncestryGraph graph = builder.Graph();
		std::vector<Index> tips;
		std::vector<Index> baseTips;

		for (const std::string_view name : tipNames)
		{
			tips.push_back(graph.Find(OidOf(name)));
		}

		for (const std::string_view name : baseNames)
		{
			baseTips.push_back(graph.Find(OidOf(name)));
		}

		const Core::LineSplit split = Core::SplitLines(graph, tips, baseTips);
		const std::uint32_t line = split.LineOf[graph.Find(OidOf(onLine))];
		return Core::MeasureLine(graph, split, line, baseTips, builder.Now());
	}

	// develop d1..d30 by three authors, days 1..30.
	void AddDevelop(GraphBuilder& builder)
	{
		builder.Chain("d", 30, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
	}

	// master m0 from d1, then merges M1 M2 M3 of three separate side branches on days 7, 11 and 15.
	void AddGitflowMaster(GraphBuilder& builder)
	{
		builder.Commit("r1", {"d2"}, 5);
		builder.Commit("h1", {"d3"}, 9);
		builder.Commit("h2", {"d4"}, 13);
		builder.Commit("m0", {"d1"}, 2);
		builder.Commit("M1", {"m0", "r1"}, 7);
		builder.Commit("M2", {"M1", "h1"}, 11);
		builder.Commit("M3", {"M2", "h2"}, 15);
	}
}

TEST_CASE("Lines: three different lines merged in the last ninety days are counted")
{
	GraphBuilder builder;
	AddDevelop(builder);
	AddGitflowMaster(builder);
	builder.SetNow(40);

	const Core::LineQualities qualities = Measure(builder, {"d30", "M3"}, {"d30"}, "M3");

	CHECK(qualities.MergedLines == 3);
	CHECK(Core::QualifiesAsBase(qualities));
}

TEST_CASE("Lines: merges older than ninety days and merges of a base's own commits do not count")
{
	SUBCASE("old merges")
	{
		GraphBuilder builder;
		AddDevelop(builder);
		AddGitflowMaster(builder);
		builder.SetNow(120);

		CHECK(Measure(builder, {"d30", "M3"}, {"d30"}, "M3").MergedLines == 0);
	}

	SUBCASE("a feature merging the base again and again")
	{
		GraphBuilder builder;
		AddDevelop(builder);
		builder.Commit("f1", {"d1"}, 31);
		builder.Commit("f2", {"f1", "d10"}, 32);
		builder.Commit("f3", {"f2", "d20"}, 33);
		builder.Commit("f4", {"f3", "d30"}, 34);

		const Core::LineQualities qualities = Measure(builder, {"d30", "f4"}, {"d30"}, "f4");

		CHECK(qualities.MergedLines == 0);
		CHECK_FALSE(Core::QualifiesAsBase(qualities));
	}
}

TEST_CASE("Lines: merging the same line again counts it once and an octopus merge counts each extra parent")
{
	SUBCASE("the same side branch twice")
	{
		GraphBuilder builder;
		AddDevelop(builder);
		builder.Commit("s1", {"d1"}, 3);
		builder.Commit("s2", {"s1"}, 6);
		builder.Commit("m0", {"d1"}, 2);
		builder.Commit("M1", {"m0", "s1"}, 4);
		builder.Commit("M2", {"M1", "s2"}, 7);
		builder.SetNow(40);

		CHECK(Measure(builder, {"d30", "M2"}, {"d30"}, "M2").MergedLines == 1);
	}

	SUBCASE("an octopus merge")
	{
		GraphBuilder builder;
		AddDevelop(builder);
		builder.Commit("x1", {"d1"}, 5);
		builder.Commit("y1", {"d1"}, 6);
		builder.Commit("z1", {"d1"}, 7);
		builder.Commit("m0", {"d1"}, 2);
		builder.Commit("O", {"m0", "x1", "y1", "z1"}, 9);
		builder.SetNow(40);

		CHECK(Measure(builder, {"d30", "O"}, {"d30"}, "O").MergedLines == 3);
	}
}

TEST_CASE("Lines: authors and span and commits are measured on the line's own stretch only")
{
	GraphBuilder builder;
	builder.Chain("m", 20, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
	builder.Commit("r1", {"m10"}, 11, "p@example.com");
	builder.Commit("r2", {"r1"}, 12, "q@example.com");
	builder.Commit("r3", {"r2"}, 14, "r@example.com");
	builder.Commit("r4", {"r3"}, 15, "p@example.com");
	builder.Commit("r5", {"r4"}, 17, "q@example.com");
	builder.Commit("r6", {"r5"}, 19, "r@example.com");

	const Core::LineQualities qualities = Measure(builder, {"m20", "r6"}, {"m20"}, "r6");

	CHECK(qualities.Authors == 3);
	CHECK(qualities.Span == GraphBuilder::Seconds(19) - GraphBuilder::Seconds(11));
	CHECK(qualities.Commits == 6);
	CHECK(Core::QualifiesAsBase(qualities));
}

TEST_CASE("Lines: a span stays positive when commit clocks run backwards")
{
	GraphBuilder builder;
	builder.Chain("m", 20, std::nullopt, 1, {});
	builder.Commit("r1", {"m10"}, 11);
	builder.Commit("r2", {"r1"}, 12);
	builder.Commit("r3", {"r2"}, 14);
	builder.Commit("r4", {"r3"}, 3);
	builder.Commit("r5", {"r4"}, 17);
	builder.Commit("r6", {"r5"}, 19);

	const Core::LineQualities qualities = Measure(builder, {"m20", "r6"}, {"m20"}, "r6");

	CHECK(qualities.Span == GraphBuilder::Seconds(19) - GraphBuilder::Seconds(3));
	CHECK(qualities.Span > 0);
}
