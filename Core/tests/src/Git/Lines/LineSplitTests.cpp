#include <doctest/doctest.h>

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineSplit.h"
#include "Core/Git/Lines/LineThresholds.h"

#include "Git/Lines/GraphBuilder.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

	using Index = Core::AncestryGraph::Index;

	struct SplitRequest
	{
		std::vector<std::string_view> Tips;
		std::vector<std::string_view> BaseTips;
	};

	std::vector<Index> FindAll(const Core::AncestryGraph& graph, const std::vector<std::string_view>& names)
	{
		std::vector<Index> commits;

		for (const std::string_view name : names)
		{
			commits.push_back(graph.Find(OidOf(name)));
		}

		return commits;
	}

	// Splits the builder's graph at the named tips and answers questions about the result by commit name.
	class NamedSplit
	{
	public:
		NamedSplit(const GraphBuilder& builder, const SplitRequest& request)
			: m_graph(builder.Graph())
		{
			const std::vector<Index> tips = FindAll(m_graph, request.Tips);
			const std::vector<Index> baseTips = FindAll(m_graph, request.BaseTips);
			m_split = Core::SplitLines(m_graph, tips, baseTips);
		}

		Index Commit(std::string_view name) const
		{
			return m_graph.Find(OidOf(name));
		}

		std::uint32_t LineOf(std::string_view name) const
		{
			return m_split.LineOf[Commit(name)];
		}

		std::uint32_t PositionOf(std::string_view name) const
		{
			return m_split.PositionOf[Commit(name)];
		}

		// The line the named commit is on; a line of nothing but kNoCommit when it is on none.
		Core::SplitLine LineAt(std::string_view name) const
		{
			const std::uint32_t line = LineOf(name);
			return line == Core::kNoLine ? Core::SplitLine{} : m_split.Lines[line];
		}

		std::size_t LineCount() const
		{
			return m_split.Lines.size();
		}

	private:
		Core::AncestryGraph m_graph;
		Core::LineSplit m_split;
	};
}

TEST_CASE("Lines: the thresholds hold the values the line model was tuned with")
{
	constexpr std::int64_t kSecondsPerDay = 86400;
	CHECK(Core::kMergedLinesForBase == 3);
	CHECK(Core::kAuthorsForBase == 3);
	CHECK(Core::kSpanForBase == 7 * kSecondsPerDay);
	CHECK(Core::kCommitsForBase == 5);
	CHECK(Core::kActiveWindow == 90 * kSecondsPerDay);
	CHECK(Core::kAuthorsBelowAStack == 3);
	CHECK(Core::kAuthorsAboveAStack == 2);
}

TEST_CASE("Lines: the side more people worked on carries the line on")
{
	GraphBuilder builder;
	builder.Chain("m", 3, std::nullopt, 1, {"a@x"});
	builder.Chain("solo", 5, "m3", 4, {"d@x"});
	builder.Chain("busy", 3, "m3", 4, {"a@x", "b@x", "c@x"});
	const NamedSplit split{builder, {.Tips = {"busy3", "solo5"}}};

	CHECK(split.LineCount() == 2);
	CHECK(split.LineOf("m3") == split.LineOf("busy3"));
	CHECK(split.LineOf("solo1") != split.LineOf("m3"));
	CHECK(split.LineAt("solo1").Join == split.Commit("m3"));
	CHECK(split.LineAt("solo1").Bottom == split.Commit("solo1"));
	CHECK(split.LineAt("solo1").Top == split.Commit("solo5"));
	CHECK(split.LineAt("m1").Bottom == split.Commit("m1"));
	CHECK(split.LineAt("m1").Top == split.Commit("busy3"));
	CHECK(split.LineAt("m1").Join == Core::AncestryGraph::kNoCommit);
	CHECK(split.PositionOf("m1") == 0);
	CHECK(split.PositionOf("busy3") == 5);
	CHECK(split.PositionOf("solo1") == 0);
	CHECK(split.PositionOf("solo5") == 4);
}

TEST_CASE("Lines: equal authors leave the line on the side with more branches above it")
{
	GraphBuilder builder;
	builder.Chain("m", 3, std::nullopt, 1, {"a@x"});
	builder.Chain("pair", 4, "m3", 4, {"a@x"});
	builder.Chain("late", 4, "m3", 10, {"a@x"});
	const NamedSplit split{builder, {.Tips = {"pair2", "pair4", "late4"}}};

	CHECK(split.LineOf("m3") == split.LineOf("pair4"));
	CHECK(split.LineOf("pair2") == split.LineOf("pair4"));
	CHECK(split.LineOf("late1") != split.LineOf("m3"));
	CHECK(split.LineAt("late1").Join == split.Commit("m3"));
}

TEST_CASE("Lines: equal authors and equal branches leave the line on the newer side")
{
	GraphBuilder builder;
	builder.Chain("m", 3, std::nullopt, 1, {"a@x"});
	builder.Chain("old", 2, "m3", 4, {"a@x"});
	builder.Chain("new", 2, "m3", 10, {"a@x"});
	const NamedSplit split{builder, {.Tips = {"old2", "new2"}}};

	CHECK(split.LineOf("m3") == split.LineOf("new2"));
	CHECK(split.LineAt("old1").Join == split.Commit("m3"));
}

TEST_CASE("Lines: a line never continues past a base's tip")
{
	GraphBuilder builder;
	builder.Chain("m", 3, std::nullopt, 1, {"a@x"});
	builder.Chain("child", 3, "m3", 4, {"a@x", "b@x", "c@x"});
	const NamedSplit split{builder, {.Tips = {"m3", "child3"}, .BaseTips = {"m3"}}};

	CHECK(split.LineOf("child1") != split.LineOf("m3"));
	CHECK(split.LineAt("child1").Join == split.Commit("m3"));
	CHECK(split.LineAt("m3").Top == split.Commit("m3"));
}

TEST_CASE("Lines: a base's first-parent chain is the continuation even when lighter")
{
	GraphBuilder builder;
	builder.Chain("m", 3, std::nullopt, 1, {"a@x"});
	builder.Chain("busy", 3, "m3", 4, {"a@x", "b@x", "c@x"});
	builder.Chain("base", 2, "m3", 4, {"a@x"});
	const NamedSplit split{builder, {.Tips = {"busy3", "base2"}, .BaseTips = {"base2"}}};

	CHECK(split.LineOf("base2") == split.LineOf("m3"));
	CHECK(split.LineOf("busy1") != split.LineOf("m3"));
	CHECK(split.LineAt("busy1").Join == split.Commit("m3"));
}

TEST_CASE("Lines: where two bases fork from one commit the base listed first keeps the line")
{
	GraphBuilder builder;
	builder.Chain("m", 3, std::nullopt, 1, {"a@x"});
	builder.Chain("left", 2, "m3", 4, {"a@x"});
	builder.Chain("right", 2, "m3", 4, {"a@x"});

	const NamedSplit rightFirst{builder, {.Tips = {"left2", "right2"}, .BaseTips = {"right2", "left2"}}};
	CHECK(rightFirst.LineOf("right2") == rightFirst.LineOf("m3"));
	CHECK(rightFirst.LineAt("left1").Join == rightFirst.Commit("m3"));

	const NamedSplit leftFirst{builder, {.Tips = {"left2", "right2"}, .BaseTips = {"left2", "right2"}}};
	CHECK(leftFirst.LineOf("left2") == leftFirst.LineOf("m3"));
	CHECK(leftFirst.LineAt("right1").Join == leftFirst.Commit("m3"));
}

TEST_CASE("Lines: one person's work on the tip of a busy line is a line of its own")
{
	GraphBuilder builder;
	builder.Chain("r", 6, std::nullopt, 1, {"a@x", "b@x", "c@x"});
	builder.Chain("c", 1, "r6", 7, {"d@x"});
	const NamedSplit split{builder, {.Tips = {"r6", "c1"}}};

	CHECK(split.LineOf("r1") == split.LineOf("r6"));
	CHECK(split.LineOf("c1") != split.LineOf("r6"));
	CHECK(split.LineAt("c1").Join == split.Commit("r6"));
	CHECK(split.LineAt("r1").Top == split.Commit("r6"));
}

TEST_CASE("Lines: a busy line goes on past a branch's tip when two people worked above it")
{
	GraphBuilder builder;
	builder.Chain("r", 6, std::nullopt, 1, {"a@x", "b@x", "c@x"});
	builder.Chain("c", 2, "r6", 7, {"d@x", "e@x"});
	const NamedSplit split{builder, {.Tips = {"r6", "c2"}}};

	CHECK(split.LineOf("c1") == split.LineOf("r6"));
	CHECK(split.LineCount() == 1);
}

TEST_CASE("Lines: a busy line goes on into one person's work when no branch sits where it starts")
{
	GraphBuilder builder;
	builder.Chain("r", 6, std::nullopt, 1, {"a@x", "b@x", "c@x"});
	builder.Chain("c", 1, "r6", 7, {"d@x"});
	const NamedSplit split{builder, {.Tips = {"c1"}}};

	CHECK(split.LineOf("c1") == split.LineOf("r6"));
	CHECK(split.LineCount() == 1);
}

TEST_CASE("Lines: a base's first-parent chain goes on past one person's work at another branch's tip")
{
	GraphBuilder builder;
	builder.Chain("r", 6, std::nullopt, 1, {"a@x", "b@x", "c@x"});
	builder.Chain("c", 1, "r6", 7, {"d@x"});
	const NamedSplit split{builder, {.Tips = {"r6", "c1"}, .BaseTips = {"c1"}}};

	CHECK(split.LineOf("c1") == split.LineOf("r6"));
	CHECK(split.LineCount() == 1);
}

TEST_CASE("Lines: in a single-author history a tip does not cut the line")
{
	GraphBuilder builder;
	builder.Chain("m", 10, std::nullopt, 1, {"a@x"});
	const NamedSplit split{builder, {.Tips = {"m5", "m10"}}};

	CHECK(split.LineOf("m5") == split.LineOf("m10"));
	CHECK(split.LineCount() == 1);
}

TEST_CASE("Lines: a commit reached only through a merge's second parent is on no line")
{
	GraphBuilder builder;
	builder.Chain("m", 3, std::nullopt, 1, {"a@x"});
	builder.Commit("s1", {"m1"}, 2, "b@x");
	builder.Commit("m4", {"m3", "s1"}, 4, "a@x");
	const NamedSplit split{builder, {.Tips = {"m4"}}};

	CHECK(split.LineOf("s1") == Core::kNoLine);
	CHECK(split.LineOf("m1") == split.LineOf("m4"));
	CHECK(split.LineCount() == 1);
}

TEST_CASE("Lines: a tip the graph lacks is skipped")
{
	GraphBuilder builder;
	builder.Chain("m", 3, std::nullopt, 1, {"a@x"});
	const NamedSplit split{builder, {.Tips = {"absent", "m3"}, .BaseTips = {"absent"}}};

	CHECK(split.LineCount() == 1);
	CHECK(split.LineAt("m1").Top == split.Commit("m3"));
}

TEST_CASE("Lines: a first-parent chain of 200000 commits splits without running out of stack")
{
	constexpr int kChainLength = 200000;
	GraphBuilder builder;
	builder.Chain("m", kChainLength, std::nullopt, 1, {"a@x", "b@x", "c@x"});
	const NamedSplit split{builder, {.Tips = {"m200000"}}};

	REQUIRE(split.LineCount() == 1);
	CHECK(split.PositionOf("m200000") == kChainLength - 1);
	CHECK(split.LineAt("m1").Bottom == split.Commit("m1"));
	CHECK(split.LineAt("m1").Top == split.Commit("m200000"));
}

TEST_CASE("Lines: five thousand branches from one commit give five thousand and one lines")
{
	constexpr int kBranchCount = 5000;
	constexpr int kBranchDay = 4;
	GraphBuilder builder;
	builder.Chain("m", 3, std::nullopt, 1, {"a@x"});
	std::vector<std::string> branchNames;

	for (int branchNumber = 1; branchNumber <= kBranchCount; ++branchNumber)
	{
		branchNames.push_back("c" + std::to_string(branchNumber));
	}

	std::vector<std::string_view> branchTips;

	for (const std::string& branchName : branchNames)
	{
		builder.Commit(branchName, {"m3"}, kBranchDay, "a@x");
		branchTips.push_back(branchName);
	}

	std::vector<std::string_view> tipsWithBase = branchTips;
	tipsWithBase.push_back("m3");
	const NamedSplit split{builder, {.Tips = tipsWithBase, .BaseTips = {"m3"}}};

	CHECK(split.LineCount() == kBranchCount + 1);
	CHECK(split.LineAt("c1").Join == split.Commit("m3"));
	CHECK(split.LineAt("c5000").Join == split.Commit("m3"));
	CHECK(split.PositionOf("c2500") == 0);

	const NamedSplit withoutBaseOneBranchCarriesTheLine{builder, {.Tips = branchTips}};
	CHECK(withoutBaseOneBranchCarriesTheLine.LineCount() == kBranchCount);
}
