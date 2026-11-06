#include <doctest/doctest.h>

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineMeasure.h"
#include "Core/Git/Lines/LineQuestions.h"
#include "Core/Git/Lines/LineSplit.h"
#include "Core/Git/Lines/LineThresholds.h"

#include "Git/Lines/GraphBuilder.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

	using Index = Core::AncestryGraph::Index;

	// Splits at the named tips and measures the line the named commit is on, which is no base's: every base that
	// reaches a commit is another base.
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
		return Core::MeasureLine(graph, split, line, baseTips, Core::CommitsReachableFrom(graph, baseTips), builder.Now());
	}

	// main m1..m20 by three authors, the base the measured lines fork from.
	void AddMain(GraphBuilder& builder)
	{
		builder.Chain("m", 20, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
	}

	// An agent's line g0 from m1, into which it merges three sub-branches forked from m3, m4 and m5 on days 40, 40 and
	// 41; the second sub-branch is written by `secondAuthor`.
	void AddAgentMergingSubBranches(GraphBuilder& builder, std::string_view secondAuthor)
	{
		AddMain(builder);
		builder.Commit("g0", {"m1"}, 2, "agent@example.com");
		builder.Commit("x1", {"m3"}, 39, "agent@example.com");
		builder.Commit("y1", {"m4"}, 39, secondAuthor);
		builder.Commit("z1", {"m5"}, 40, "agent@example.com");
		builder.Commit("G1", {"g0", "x1"}, 40, "agent@example.com");
		builder.Commit("G2", {"G1", "y1"}, 40, "agent@example.com");
		builder.Commit("G3", {"G2", "z1"}, 41, "agent@example.com");
		builder.SetNow(50);
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

TEST_CASE("Lines: one person's lines merged over a week qualify")
{
	// The gitflow master: every merged tip is one author's, merged on days 7 to 15.
	GraphBuilder builder;
	AddDevelop(builder);
	AddGitflowMaster(builder);
	builder.SetNow(40);

	const Core::LineQualities qualities = Measure(builder, {"d30", "M3"}, {"d30"}, "M3");

	CHECK(qualities.MergedAuthors == 1);
	CHECK(qualities.MergeSpan == GraphBuilder::Seconds(15) - GraphBuilder::Seconds(7));
	CHECK(Core::QualifiesByMerges(qualities));
}

TEST_CASE("Lines: one person's three lines merged within a day do not qualify")
{
	GraphBuilder builder;
	AddAgentMergingSubBranches(builder, "agent@example.com");

	const Core::LineQualities qualities = Measure(builder, {"m20", "G3"}, {"m20"}, "G3");

	CHECK(qualities.MergedLines == 3);
	CHECK(qualities.MergedAuthors == 1);
	CHECK(qualities.MergeSpan == GraphBuilder::Seconds(41) - GraphBuilder::Seconds(40));
	CHECK_FALSE(Core::QualifiesByMerges(qualities));
	CHECK_FALSE(Core::QualifiesAsBase(qualities));
}

TEST_CASE("Lines: two people's lines merged within a day qualify")
{
	GraphBuilder builder;
	AddAgentMergingSubBranches(builder, "dev@example.com");

	const Core::LineQualities qualities = Measure(builder, {"m20", "G3"}, {"m20"}, "G3");

	CHECK(qualities.MergedLines == 3);
	CHECK(qualities.MergedAuthors == 2);
	CHECK(Core::QualifiesByMerges(qualities));
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
	AddMain(builder);
	builder.Commit("r1", {"m10"}, 11, "p@example.com", GraphBuilder::kService);
	builder.Commit("r2", {"r1"}, 12, "q@example.com", GraphBuilder::kService);
	builder.Commit("r3", {"r2"}, 14, "r@example.com", GraphBuilder::kService);
	builder.Commit("r4", {"r3"}, 15, "p@example.com", GraphBuilder::kService);
	builder.Commit("r5", {"r4"}, 17, "q@example.com", GraphBuilder::kService);
	builder.Commit("r6", {"r5"}, 19, "r@example.com", GraphBuilder::kService);

	const Core::LineQualities qualities = Measure(builder, {"m20", "r6"}, {"m20"}, "r6");

	CHECK(qualities.Authors == 3);
	CHECK(qualities.Span == GraphBuilder::Seconds(19) - GraphBuilder::Seconds(11));
	CHECK(qualities.Commits == 6);
	CHECK(Core::QualifiesAsBase(qualities));
}

TEST_CASE("Lines: only commits someone other than their author committed count toward the authors")
{
	// r1..r6 by three people over eight days, from m10.
	GraphBuilder builder;
	AddMain(builder);

	SUBCASE("each committed by its author")
	{
		builder.Chain("r", 6, "m10", 11, {"p@example.com", "q@example.com", "r@example.com"});

		const Core::LineQualities qualities = Measure(builder, {"m20", "r6"}, {"m20"}, "r6");

		CHECK(qualities.Authors == 0);
		CHECK(qualities.Commits == 6);
		CHECK_FALSE(Core::QualifiesAsBase(qualities));
	}

	SUBCASE("two people's committed by the service, the third's by its author")
	{
		builder.Commit("r1", {"m10"}, 11, "p@example.com", GraphBuilder::kService);
		builder.Commit("r2", {"r1"}, 12, "q@example.com", GraphBuilder::kService);
		builder.Commit("r3", {"r2"}, 14, "r@example.com");
		builder.Commit("r4", {"r3"}, 15, "p@example.com", GraphBuilder::kService);
		builder.Commit("r5", {"r4"}, 17, "q@example.com", "p@example.com");
		builder.Commit("r6", {"r5"}, 19, "r@example.com");

		const Core::LineQualities qualities = Measure(builder, {"m20", "r6"}, {"m20"}, "r6");

		CHECK(qualities.Authors == 2);
		CHECK_FALSE(Core::QualifiesAsBase(qualities));
	}
}

TEST_CASE("Lines: an author whose only commits on the line are merges does not count")
{
	// p and q's work, landed by the service; r only merges main into the line, on days 13 and 19.
	GraphBuilder builder;
	AddMain(builder);
	builder.Commit("r1", {"m10"}, 11, "p@example.com", GraphBuilder::kService);
	builder.Commit("r2", {"r1"}, 12, "q@example.com", GraphBuilder::kService);
	builder.Commit("S1", {"r2", "m13"}, 13, "r@example.com", GraphBuilder::kService);
	builder.Commit("r3", {"S1"}, 15, "p@example.com", GraphBuilder::kService);
	builder.Commit("r4", {"r3"}, 17, "q@example.com", GraphBuilder::kService);
	builder.Commit("S2", {"r4", "m18"}, 19, "r@example.com", GraphBuilder::kService);

	const Core::LineQualities qualities = Measure(builder, {"m20", "S2"}, {"m20"}, "S2");

	CHECK(qualities.Authors == 2);
	CHECK(qualities.Commits == 6);
	CHECK_FALSE(Core::QualifiesAsBase(qualities));
}

TEST_CASE("Lines: a merge's author does not count but its time counts toward the span")
{
	// Three people's work on days 11 to 14, then s merges main into the line on day 19.
	GraphBuilder builder;
	AddMain(builder);
	builder.Commit("r1", {"m10"}, 11, "p@example.com", GraphBuilder::kService);
	builder.Commit("r2", {"r1"}, 12, "q@example.com", GraphBuilder::kService);
	builder.Commit("r3", {"r2"}, 13, "r@example.com", GraphBuilder::kService);
	builder.Commit("r4", {"r3"}, 14, "p@example.com", GraphBuilder::kService);
	builder.Commit("S1", {"r4", "m18"}, 19, "s@example.com", GraphBuilder::kService);

	const Core::LineQualities qualities = Measure(builder, {"m20", "S1"}, {"m20"}, "S1");

	CHECK(qualities.Authors == 3);
	CHECK(qualities.Span == GraphBuilder::Seconds(19) - GraphBuilder::Seconds(11));
	CHECK(qualities.Commits == 5);
	CHECK(Core::QualifiesAsBase(qualities));
}

TEST_CASE("Lines: commits a base already reaches are not the line's own")
{
	SUBCASE("a topic built on a pull request main has merged")
	{
		// The pull request p1..p6, three people's work from m5 over eight days, is merged into main at M; topic t1 was
		// built on its last commit.
		GraphBuilder builder;
		AddMain(builder);
		builder.Commit("p1", {"m5"}, 6, "p@example.com", GraphBuilder::kService);
		builder.Commit("p2", {"p1"}, 7, "q@example.com", GraphBuilder::kService);
		builder.Commit("p3", {"p2"}, 9, "r@example.com", GraphBuilder::kService);
		builder.Commit("p4", {"p3"}, 10, "p@example.com", GraphBuilder::kService);
		builder.Commit("p5", {"p4"}, 12, "q@example.com", GraphBuilder::kService);
		builder.Commit("p6", {"p5"}, 14, "r@example.com", GraphBuilder::kService);
		builder.Commit("M", {"m20", "p6"}, 21, "a@example.com", GraphBuilder::kService);
		builder.Commit("t1", {"p6"}, 22, "t@example.com");

		const Core::LineQualities qualities = Measure(builder, {"M", "t1"}, {"M"}, "t1");

		CHECK(qualities.Commits == 1);
		CHECK(qualities.Authors == 0);
		CHECK(qualities.Span == 0);
		CHECK_FALSE(Core::QualifiesAsBase(qualities));
	}

	SUBCASE("a branch on an old trunk that a merge left as main's second parent")
	{
		// o1..o10 was main until a topic s1..s3 from o5 merged it in at X and main went on from X: o6..o10 now hang off
		// main's second parent. old/fix was made on o10.
		GraphBuilder builder;
		builder.Chain("o", 10, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"}, GraphBuilder::kService);
		builder.Chain("s", 3, "o5", 11, {"d@example.com"});
		builder.Commit("X", {"s3", "o10"}, 14, "d@example.com");
		builder.Chain("n", 3, "X", 15, {"a@example.com"});
		builder.Commit("b1", {"o10"}, 18, "e@example.com");

		const Core::LineQualities qualities = Measure(builder, {"n3", "b1"}, {"n3"}, "b1");

		CHECK(qualities.Commits == 1);
		CHECK(qualities.Authors == 0);
		CHECK_FALSE(Core::QualifiesAsBase(qualities));
	}
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
