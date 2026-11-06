#include <doctest/doctest.h>

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineAnswers.h"
#include "Core/Git/Lines/LineMeasure.h"
#include "Core/Git/Lines/LineModel.h"

#include "Git/Lines/GraphBuilder.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace {

	Core::LineModel Compute(const GraphBuilder& builder, const Core::LineAnswers& answers)
	{
		const Core::AncestryGraph graph = builder.Graph();
		return Core::ComputeLines({.Graph = graph, .Refs = builder.Refs(), .Answers = answers, .Now = builder.Now()});
	}

	Core::LineModel Compute(const GraphBuilder& builder)
	{
		return Compute(builder, Core::LineAnswers{});
	}

	bool AnyBaseIsGuessed(const Core::LineModel& model)
	{
		for (const Core::LineBase& base : model.Bases)
		{
			if (base.Reason == Core::BaseReason::MainLineGuess)
			{
				return true;
			}
		}

		return false;
	}

	bool IsBase(const Core::LineModel& model, std::size_t ref)
	{
		for (const Core::LineBase& base : model.Bases)
		{
			if (base.Ref == ref)
			{
				return true;
			}
		}

		return false;
	}

	// GraphBuilder::kNoRef when the ref is a base or is compared with nothing, so that a check against a ref fails.
	std::size_t ComparedRef(const Core::LineModel& model, std::size_t ref)
	{
		for (const Core::BranchLine& branch : model.Branches)
		{
			if (branch.Ref == ref)
			{
				return branch.ComparedWith.value_or(GraphBuilder::kNoRef);
			}
		}

		return GraphBuilder::kNoRef;
	}

	// develop d1..dN by three authors on days 1..N.
	void AddDevelop(GraphBuilder& builder, int commitCount)
	{
		builder.Chain("d", commitCount, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
	}

	std::string DevelopCommit(int number)
	{
		return "d" + std::to_string(number);
	}

	// gitflow's master: m0 on develop, then merges M1 M2 M3 of three separate side branches, each forked from
	// develop. With no shift it forks from d1..d4 and merges on days 7, 11 and 15; a shift moves every commit and
	// every fork point that many days and commits later.
	void AddGitflowMaster(GraphBuilder& builder, int shift)
	{
		builder.Commit("r1", {DevelopCommit(2 + shift)}, 5 + shift);
		builder.Commit("h1", {DevelopCommit(3 + shift)}, 9 + shift);
		builder.Commit("h2", {DevelopCommit(4 + shift)}, 13 + shift);
		builder.Commit("m0", {DevelopCommit(1 + shift)}, 2 + shift);
		builder.Commit("M1", {"m0", "r1"}, 7 + shift);
		builder.Commit("M2", {"M1", "h1"}, 11 + shift);
		builder.Commit("M3", {"M2", "h2"}, 15 + shift);
	}

	// main m1..m20 by three authors; release r1..r6 from m10 by three others over eight days, with no merges, each
	// landed by the service. origin/main, the default, points at `mainTip`.
	void AddSquashOnlyRelease(GraphBuilder& builder, std::string_view mainTip = "m20")
	{
		builder.Chain("m", 20, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
		builder.Commit("r1", {"m10"}, 11, "p@example.com", GraphBuilder::kService);
		builder.Commit("r2", {"r1"}, 12, "q@example.com", GraphBuilder::kService);
		builder.Commit("r3", {"r2"}, 14, "r@example.com", GraphBuilder::kService);
		builder.Commit("r4", {"r3"}, 15, "p@example.com", GraphBuilder::kService);
		builder.Commit("r5", {"r4"}, 17, "q@example.com", GraphBuilder::kService);
		builder.Commit("r6", {"r5"}, 19, "r@example.com", GraphBuilder::kService);
		builder.Ref("refs/remotes/origin/main", mainTip, true);
		builder.Ref("refs/remotes/origin/release/2", "r6");
	}

	// main m1..m20 by three authors, the server's default.
	void AddDefaultMain(GraphBuilder& builder)
	{
		builder.Chain("m", 20, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
		builder.Ref("refs/remotes/origin/main", "m20", true);
	}

	// upstream's main u1..u30 by three authors, landed by the service; the fork's origin/main stopped at u5.
	void AddFork(GraphBuilder& builder, bool upstreamReportsDefault)
	{
		builder.Chain("u", 30, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"}, GraphBuilder::kService);
		builder.Ref("refs/remotes/origin/main", "u5", true);
		builder.Ref("refs/remotes/upstream/main", "u30", upstreamReportsDefault);
		builder.SetNow(40);
	}

	// The fork's origin/main stopped at u5; upstream's main line `upstreamMainLine` is u1..u30 by three authors, and
	// upstream/feature, f1..f8 from u5 by four others over seven days, is a base by its authors and the heavier line
	// above u5. The service landed every commit.
	void AddForkWithHeavierFeature(GraphBuilder& builder, std::string_view upstreamMainLine, bool upstreamReportsDefault)
	{
		builder.Chain("u", 30, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"}, GraphBuilder::kService);
		builder.Chain("f", 8, "u5", 6, {"d@example.com", "e@example.com", "f@example.com", "g@example.com"}, GraphBuilder::kService);
		builder.Ref("refs/remotes/origin/main", "u5", true);
		builder.Ref(upstreamMainLine, "u30", upstreamReportsDefault);
		builder.Ref("refs/remotes/upstream/feature", "f8");
		builder.SetNow(40);
	}

	// Local branches only. main m1..m20 by three authors; feature/x x1..x5 from m10 by five others in five days,
	// so that with no base pinned its side, not main's, carries m1..m10 on; feature/y on feature/x's tip; and
	// four fixes forked from m15..m18.
	void AddLocalRepository(GraphBuilder& builder)
	{
		builder.Chain("m", 20, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
		builder.Chain("x", 5, "m10", 11, {"d@example.com", "e@example.com", "f@example.com", "g@example.com", "h@example.com"});
		builder.Commit("y1", {"x5"}, 16, "d@example.com");
		builder.Commit("fix1", {"m15"}, 21, "i@example.com");
		builder.Commit("fix2", {"m16"}, 22, "i@example.com");
		builder.Commit("fix3", {"m17"}, 23, "i@example.com");
		builder.Commit("fix4", {"m18"}, 24, "i@example.com");
		builder.Ref("refs/heads/main", "m20");
		builder.Ref("refs/heads/feature/x", "x5");
		builder.Ref("refs/heads/feature/y", "y1");
		builder.Ref("refs/heads/fix/1", "fix1");
		builder.Ref("refs/heads/fix/2", "fix2");
		builder.Ref("refs/heads/fix/3", "fix3");
		builder.Ref("refs/heads/fix/4", "fix4");
		builder.SetNow(30);
	}
}

TEST_CASE("Lines: the server's default branch is a base")
{
	GraphBuilder builder;
	builder.Chain("m", 10, std::nullopt, 1, {});
	builder.Commit("t1", {"m5"}, 11);
	builder.Ref("refs/remotes/origin/main", "m10", true);
	builder.Ref("refs/heads/topic", "t1");
	builder.SetNow(20);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 1);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::ServerDefault);
	CHECK(model.Bases[0].Active);
	CHECK(model.Bases[0].Qualities.Commits == 10);
	CHECK(model.OldCopies.empty());
	CHECK_FALSE(model.MainLineGuessed);
}

TEST_CASE("Lines: gitflow master is a base because three lines were merged into it")
{
	GraphBuilder builder;
	AddDevelop(builder, 30);
	AddGitflowMaster(builder, 0);
	builder.Ref("refs/remotes/origin/develop", "d30", true);
	builder.Ref("refs/remotes/origin/master", "M3");
	builder.SetNow(40);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 2);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/origin/develop"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::ServerDefault);
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/remotes/origin/master"));
	CHECK(model.Bases[1].Reason == Core::BaseReason::MergedLines);
	CHECK(model.Bases[1].Qualities.MergedLines == 3);
	CHECK(model.Bases[1].Active);
}

TEST_CASE("Lines: a squash-only release line is a base because three people worked on it for a week")
{
	GraphBuilder builder;
	AddSquashOnlyRelease(builder);
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 2);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/remotes/origin/release/2"));
	CHECK(model.Bases[1].Reason == Core::BaseReason::Authors);
	CHECK(model.Bases[1].Qualities.Authors == 3);
}

TEST_CASE("Lines: a branch built on the tip of a busy release line does not become the base in its place")
{
	GraphBuilder builder;
	AddSquashOnlyRelease(builder);
	builder.Commit("c1", {"r6"}, 20, "d@example.com");
	builder.Ref("refs/remotes/origin/cp", "c1");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 2);
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/remotes/origin/release/2"));
	CHECK(model.Bases[1].Reason == Core::BaseReason::Authors);
	CHECK(model.Bases[1].Qualities.Authors == 3);
	CHECK_FALSE(IsBase(model, builder.RefIndex("refs/remotes/origin/cp")));
}

TEST_CASE("Lines: a release line is still a base when a pull request by two people sits on its tip")
{
	// Two authors above the tip go on with the line, so the line is named after the pull request (a known weak spot of
	// the split); the release line's own work, landed by the service, still makes it a base.
	GraphBuilder builder;
	AddSquashOnlyRelease(builder);
	builder.Commit("c1", {"r6"}, 20, "d@example.com");
	builder.Commit("c2", {"c1"}, 21, "e@example.com");
	builder.Ref("refs/remotes/origin/cp", "c2");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 2);
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/remotes/origin/cp"));
	CHECK(model.Bases[1].Reason == Core::BaseReason::Authors);
	CHECK(model.Bases[1].Qualities.Authors == 3);
}

TEST_CASE("Lines: one person under two e-mails with a coding agent is not a base")
{
	// topic t1..t9 from m10 over nine days: the developer's commits under a home and a work e-mail, pushed by the
	// developer, and the agent's, which the developer committed.
	GraphBuilder builder;
	AddDefaultMain(builder);
	builder.Chain("t", 9, "m10", 21, {"dev@home.example", "dev@work.example"});
	builder.Commit("t10", {"t9"}, 30, "agent@example.com", "dev@work.example");
	builder.Commit("t11", {"t10"}, 31, "agent@example.com", "dev@work.example");
	builder.Ref("refs/remotes/origin/topic", "t11");
	builder.SetNow(40);

	const Core::LineModel model = Compute(builder);

	CHECK_FALSE(IsBase(model, builder.RefIndex("refs/remotes/origin/topic")));
	CHECK(ComparedRef(model, builder.RefIndex("refs/remotes/origin/topic")) == builder.RefIndex("refs/remotes/origin/main"));
}

TEST_CASE("Lines: a topic whose reviewer merged the trunk into it is not a base")
{
	// Two people's work, landed by the service; a reviewer twice brings the topic up to date with main.
	GraphBuilder builder;
	AddDefaultMain(builder);
	builder.Commit("t1", {"m10"}, 11, "p@example.com", GraphBuilder::kService);
	builder.Commit("t2", {"t1"}, 12, "q@example.com", GraphBuilder::kService);
	builder.Commit("S1", {"t2", "m14"}, 14, "rev@example.com", GraphBuilder::kService);
	builder.Commit("t3", {"S1"}, 15, "p@example.com", GraphBuilder::kService);
	builder.Commit("S2", {"t3", "m19"}, 19, "rev@example.com", GraphBuilder::kService);
	builder.Ref("refs/remotes/origin/topic", "S2");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);

	CHECK_FALSE(IsBase(model, builder.RefIndex("refs/remotes/origin/topic")));
}

TEST_CASE("Lines: an agent merging three of its own sub-branches within a day is not a base")
{
	GraphBuilder builder;
	AddDefaultMain(builder);
	builder.Commit("g0", {"m1"}, 2, "agent@example.com");
	builder.Commit("x1", {"m3"}, 39, "agent@example.com");
	builder.Commit("y1", {"m4"}, 39, "agent@example.com");
	builder.Commit("z1", {"m5"}, 40, "agent@example.com");
	builder.Commit("G1", {"g0", "x1"}, 40, "agent@example.com");
	builder.Commit("G2", {"G1", "y1"}, 40, "agent@example.com");
	builder.Commit("G3", {"G2", "z1"}, 41, "agent@example.com");
	builder.Ref("refs/remotes/origin/agents/integration", "G3");
	builder.SetNow(50);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 1);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
}

TEST_CASE("Lines: merging the trunk into a feature three times does not make the feature a base")
{
	GraphBuilder builder;
	AddDevelop(builder, 30);
	builder.Commit("f1", {"d1"}, 31);
	builder.Commit("f2", {"f1", "d10"}, 32);
	builder.Commit("f3", {"f2", "d20"}, 33);
	builder.Commit("f4", {"f3", "d30"}, 34);
	builder.Ref("refs/remotes/origin/develop", "d30", true);
	builder.Ref("refs/remotes/origin/feature", "f4");
	builder.SetNow(40);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 1);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/origin/develop"));
}

TEST_CASE("Lines: a fork's stale default is an old copy when both remotes report a default")
{
	GraphBuilder builder;
	AddFork(builder, true);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 1);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/upstream/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::ServerDefault);
	CHECK(model.Bases[0].Qualities.Commits == 30);
	REQUIRE(model.OldCopies.size() == 1);
	CHECK(model.OldCopies[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(model.OldCopies[0].Of == builder.RefIndex("refs/remotes/upstream/main"));
	CHECK_FALSE(model.MainLineGuessed);
}

TEST_CASE("Lines: a fork's stale default is an old copy when only the fork reports a default")
{
	// The base the stale default copies takes over as the main line, so nothing is guessed or asked.
	GraphBuilder builder;
	AddFork(builder, false);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 1);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/upstream/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::Authors);
	REQUIRE(model.OldCopies.size() == 1);
	CHECK(model.OldCopies[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(model.OldCopies[0].Of == builder.RefIndex("refs/remotes/upstream/main"));
	CHECK_FALSE(model.MainLineGuessed);
}

TEST_CASE("Lines: a stale default is an old copy of upstream's main line even when an upstream release line is listed first")
{
	// Only the fork reports a default. upstream/release/1 forked from u10 by two people and is kept from an earlier
	// read, so it is listed before upstream/main, which the rules find; both first-parent chains hold u5.
	GraphBuilder builder;
	AddFork(builder, false);
	builder.Chain("r", 4, "u10", 11, {"p@example.com", "q@example.com"});
	builder.Ref("refs/remotes/upstream/release/1", "r4");
	Core::LineAnswers answers;
	answers.KeptBases = {"refs/remotes/upstream/release/1"};

	const Core::LineModel model = Compute(builder, answers);

	REQUIRE(model.OldCopies.size() == 1);
	CHECK(model.OldCopies[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(model.OldCopies[0].Of == builder.RefIndex("refs/remotes/upstream/main"));
	REQUIRE_FALSE(model.Bases.empty());
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/upstream/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::Authors);
	CHECK(model.Bases[0].Qualities.Commits == 20); // u1..u10 are release/1's history too, so its own work starts at u11
	CHECK(IsBase(model, builder.RefIndex("refs/remotes/upstream/release/1")));
	CHECK_FALSE(IsBase(model, builder.RefIndex("refs/remotes/origin/main")));
	CHECK_FALSE(model.MainLineGuessed);
}

TEST_CASE("Lines: an integration branch on a remote with fewer branches does not make the default an old copy")
{
	// origin has the default and two features; a colleague's remote has one integration branch built on origin/main's
	// tip by three people over eight days, landed by the service, which the rules find as a base.
	GraphBuilder builder;
	builder.Chain("u", 20, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
	builder.Commit("f1", {"u10"}, 11, "d@example.com");
	builder.Commit("g1", {"u12"}, 13, "d@example.com");
	builder.Chain("i", 9, "u20", 21, {"p@example.com", "q@example.com", "r@example.com"}, GraphBuilder::kService);
	builder.Ref("refs/remotes/origin/main", "u20", true);
	builder.Ref("refs/remotes/origin/feature/1", "f1");
	builder.Ref("refs/remotes/origin/feature/2", "g1");
	builder.Ref("refs/remotes/alice/integration", "i9");
	builder.SetNow(40);

	const Core::LineModel model = Compute(builder);

	CHECK(model.OldCopies.empty());
	REQUIRE(model.Bases.size() == 2);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::ServerDefault);
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/remotes/alice/integration"));
	CHECK(model.Bases[1].Reason == Core::BaseReason::Authors);
	CHECK_FALSE(model.MainLineGuessed);
}

TEST_CASE("Lines: a stale default hands the main line to the other remote's default over a heavier line")
{
	GraphBuilder builder;
	AddForkWithHeavierFeature(builder, "refs/remotes/upstream/main", true);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 2);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/upstream/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::ServerDefault);
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/remotes/upstream/feature"));
	CHECK(model.Bases[1].Reason == Core::BaseReason::Authors);
	REQUIRE(model.OldCopies.size() == 1);
	CHECK(model.OldCopies[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(model.OldCopies[0].Of == builder.RefIndex("refs/remotes/upstream/main"));
	CHECK_FALSE(model.MainLineGuessed);
}

TEST_CASE("Lines: a stale default hands the main line to the user's main line answer")
{
	GraphBuilder builder;
	AddForkWithHeavierFeature(builder, "refs/remotes/upstream/develop", false);
	Core::LineAnswers answers;
	answers.MainLine = "refs/remotes/upstream/develop";

	const Core::LineModel model = Compute(builder, answers);

	REQUIRE(model.Bases.size() == 2);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/upstream/develop"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::MainLineByUser);
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/remotes/upstream/feature"));
	REQUIRE(model.OldCopies.size() == 1);
	CHECK(model.OldCopies[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(model.OldCopies[0].Of == builder.RefIndex("refs/remotes/upstream/develop"));
	CHECK_FALSE(model.MainLineGuessed);
}

TEST_CASE("Lines: without another default the main line after a stale copy is voted among the candidates")
{
	// Only the fork reports a default, at u5. upstream/a (three authors) and upstream/b (four, so the heavier line
	// above u5) both go on from it and are bases by their authors, whose work the service landed; three active fixes
	// fork from upstream/a and none from upstream/b.
	GraphBuilder builder;
	builder.Chain("u", 5, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
	builder.Chain("a", 8, "u5", 6, {"p@example.com", "q@example.com", "r@example.com"}, GraphBuilder::kService);
	builder.Chain("b", 8, "u5", 6, {"s@example.com", "t@example.com", "v@example.com", "w@example.com"}, GraphBuilder::kService);
	builder.Commit("x1", {"a2"}, 14, "x@example.com");
	builder.Commit("y1", {"a3"}, 15, "y@example.com");
	builder.Commit("z1", {"a4"}, 16, "z@example.com");
	builder.Ref("refs/remotes/origin/main", "u5", true);
	builder.Ref("refs/remotes/upstream/a", "a8");
	builder.Ref("refs/remotes/upstream/b", "b8");
	builder.Ref("refs/heads/fix/1", "x1");
	builder.Ref("refs/heads/fix/2", "y1");
	builder.Ref("refs/heads/fix/3", "z1");
	builder.SetNow(40);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 2);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/upstream/a"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::Authors);
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/remotes/upstream/b"));
	REQUIRE(model.OldCopies.size() == 1);
	CHECK(model.OldCopies[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(model.OldCopies[0].Of == builder.RefIndex("refs/remotes/upstream/a"));
	CHECK_FALSE(model.MainLineGuessed);
	CHECK_FALSE(AnyBaseIsGuessed(model));
	CHECK(ComparedRef(model, builder.RefIndex("refs/heads/fix/1")) == builder.RefIndex("refs/remotes/upstream/a"));
}

TEST_CASE("Lines: with no branch active the main line after a stale copy is the line going on from its tip")
{
	// No vote is cast. The kept upstream/release/1 is listed before upstream/main, but upstream/main's line is the
	// one that goes on from u5 with nothing pinned.
	GraphBuilder builder;
	AddFork(builder, false);
	builder.Chain("r", 4, "u10", 11, {"p@example.com", "q@example.com"});
	builder.Ref("refs/remotes/upstream/release/1", "r4");
	builder.SetNow(300);
	Core::LineAnswers answers;
	answers.KeptBases = {"refs/remotes/upstream/release/1"};
	answers.BaseOrder = {"refs/remotes/upstream/release/1"};

	const Core::LineModel model = Compute(builder, answers);

	REQUIRE(model.OldCopies.size() == 1);
	CHECK(model.OldCopies[0].Of == builder.RefIndex("refs/remotes/upstream/main"));
	REQUIRE(model.Bases.size() == 2);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/upstream/main"));
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/remotes/upstream/release/1"));
	CHECK_FALSE(model.MainLineGuessed);
}

TEST_CASE("Lines: with no default anywhere the most forked-from line is guessed")
{
	GraphBuilder builder;
	AddLocalRepository(builder);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 1);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/heads/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::MainLineGuess);
	CHECK(model.MainLineGuessed);
}

TEST_CASE("Lines: with no branch active in ninety days the guess is the top of the line with the most branches")
{
	GraphBuilder builder;
	builder.Chain("m", 10, std::nullopt, 1, {});
	builder.Commit("b1", {"m3"}, 3);
	builder.Commit("b2", {"m5"}, 5);
	builder.Chain("o", 3, std::nullopt, 1, {});
	builder.Ref("refs/heads/orphan", "o3");
	builder.Ref("refs/heads/old/1", "b1");
	builder.Ref("refs/heads/old/2", "b2");
	builder.Ref("refs/heads/main", "m10");
	builder.SetNow(300);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 1);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/heads/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::MainLineGuess);
	CHECK_FALSE(model.Bases[0].Active);
	CHECK(model.MainLineGuessed);
}

TEST_CASE("Lines: the user's main line replaces the guess")
{
	GraphBuilder builder;
	AddLocalRepository(builder);
	Core::LineAnswers answers;
	answers.MainLine = "refs/heads/feature/x";

	const Core::LineModel model = Compute(builder, answers);

	REQUIRE_FALSE(model.Bases.empty());
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/heads/feature/x"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::MainLineByUser);
	CHECK_FALSE(model.MainLineGuessed);
	CHECK_FALSE(AnyBaseIsGuessed(model));
}

TEST_CASE("Lines: removed and added and kept bases follow the answers")
{
	// develop d1..d150 is the default; master merges three lines on days 107..115 and qualifies on its own;
	// release/1 merged three lines on days 13..40, which by day 160 is long ago.
	GraphBuilder builder;
	AddDevelop(builder, 150);
	AddGitflowMaster(builder, 100);
	builder.Commit("s0", {"d10"}, 11);
	builder.Commit("x1", {"d11"}, 12);
	builder.Commit("S1", {"s0", "x1"}, 13);
	builder.Commit("y1", {"d12"}, 14);
	builder.Commit("S2", {"S1", "y1"}, 15);
	builder.Commit("z1", {"d13"}, 16);
	builder.Commit("S3", {"S2", "z1"}, 40);
	builder.Commit("a1", {"d148"}, 151);
	builder.Ref("refs/remotes/origin/develop", "d150", true);
	builder.Ref("refs/remotes/origin/master", "M3");
	builder.Ref("refs/remotes/origin/release/1", "S3");
	builder.Ref("refs/heads/feature/a", "a1");
	builder.SetNow(160);

	Core::LineAnswers answers;
	answers.RemovedBases = {"refs/remotes/origin/master"};
	answers.AddedBases = {"refs/heads/feature/a"};
	answers.KeptBases = {"refs/remotes/origin/release/1"};

	const Core::LineModel model = Compute(builder, answers);

	REQUIRE(model.Bases.size() == 3);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/origin/develop"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::ServerDefault);
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/heads/feature/a"));
	CHECK(model.Bases[1].Reason == Core::BaseReason::AddedByUser);
	CHECK(model.Bases[2].Ref == builder.RefIndex("refs/remotes/origin/release/1"));
	CHECK(model.Bases[2].Reason == Core::BaseReason::Kept);
	CHECK_FALSE(model.Bases[2].Active);
	CHECK_FALSE(Core::QualifiesAsBase(model.Bases[2].Qualities));
	CHECK_FALSE(IsBase(model, builder.RefIndex("refs/remotes/origin/master")));
}

TEST_CASE("Lines: a kept base whose line still qualifies carries the rule's reason")
{
	GraphBuilder builder;
	AddSquashOnlyRelease(builder);
	builder.SetNow(30);
	Core::LineAnswers answers;
	answers.KeptBases = {"refs/remotes/origin/release/2"};

	const Core::LineModel model = Compute(builder, answers);

	REQUIRE(model.Bases.size() == 2);
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/remotes/origin/release/2"));
	CHECK(model.Bases[1].Reason == Core::BaseReason::Authors);
}

TEST_CASE("Lines: a kept base whose work another base now reaches stays a base and is told as kept")
{
	// main merged release/2 at m21, so every commit of release/2's line is main's history too.
	GraphBuilder builder;
	builder.Commit("m21", {"m20", "r6"}, 21, "a@example.com", GraphBuilder::kService);
	AddSquashOnlyRelease(builder, "m21");
	builder.SetNow(30);
	Core::LineAnswers answers;
	answers.KeptBases = {"refs/remotes/origin/release/2"};

	const Core::LineModel model = Compute(builder, answers);

	REQUIRE(model.Bases.size() == 2);
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/remotes/origin/release/2"));
	CHECK(model.Bases[1].Reason == Core::BaseReason::Kept);
	CHECK(model.Bases[1].Qualities.Commits == 0);
}

TEST_CASE("Lines: a base added on a kept base's tip does not take the kept base's line")
{
	// release/2 was found and kept; the user then added staging at its tip. backport forks from release/2's line
	// below that tip.
	GraphBuilder builder;
	AddSquashOnlyRelease(builder);
	builder.Commit("bp1", {"r3"}, 20, "d@example.com");
	builder.Ref("refs/heads/staging", "r6");
	builder.Ref("refs/heads/backport", "bp1");
	builder.SetNow(30);
	Core::LineAnswers answers;
	answers.KeptBases = {"refs/remotes/origin/release/2"};
	answers.AddedBases = {"refs/heads/staging"};
	answers.BaseOrder = {"refs/remotes/origin/release/2", "refs/heads/staging"};

	const Core::LineModel model = Compute(builder, answers);

	REQUIRE(model.Bases.size() == 3);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/remotes/origin/release/2"));
	CHECK(model.Bases[1].Qualities.Commits == 6);
	CHECK(model.Bases[2].Ref == builder.RefIndex("refs/heads/staging"));
	CHECK(model.Bases[2].Reason == Core::BaseReason::AddedByUser);
	CHECK(ComparedRef(model, builder.RefIndex("refs/heads/backport")) == builder.RefIndex("refs/remotes/origin/release/2"));
}

TEST_CASE("Lines: bases are ordered by when each became a base")
{
	// staging was added before release/2 was found and kept.
	GraphBuilder builder;
	AddSquashOnlyRelease(builder);
	builder.Commit("s1", {"m15"}, 21, "d@example.com");
	builder.Ref("refs/heads/staging", "s1");
	builder.SetNow(30);
	Core::LineAnswers answers;
	answers.KeptBases = {"refs/remotes/origin/release/2"};
	answers.AddedBases = {"refs/heads/staging"};
	answers.BaseOrder = {"refs/heads/staging", "refs/remotes/origin/release/2"};

	const Core::LineModel model = Compute(builder, answers);

	REQUIRE(model.Bases.size() == 3);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::ServerDefault);
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/heads/staging"));
	CHECK(model.Bases[1].Reason == Core::BaseReason::AddedByUser);
	CHECK(model.Bases[2].Ref == builder.RefIndex("refs/remotes/origin/release/2"));
}

TEST_CASE("Lines: a reference whose tip the graph lacks is never a base")
{
	SUBCASE("beside a default that is in the graph")
	{
		GraphBuilder builder;
		builder.Chain("m", 10, std::nullopt, 1, {});
		builder.Ref("refs/remotes/upstream/main", "never-read", true);
		builder.Ref("refs/remotes/origin/main", "m10", true);
		builder.Ref("refs/heads/added", "never-read-either");
		Core::LineAnswers answers;
		answers.AddedBases = {"refs/heads/added", "refs/heads/never-made"};
		answers.KeptBases = {"refs/remotes/upstream/main"};

		const Core::LineModel model = Compute(builder, answers);

		REQUIRE(model.Bases.size() == 1);
		CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
		CHECK(model.OldCopies.empty());
	}

	SUBCASE("as the only reference with nothing to guess from")
	{
		GraphBuilder builder;
		builder.Chain("m", 10, std::nullopt, 1, {});
		builder.Ref("refs/heads/main", "never-read");
		Core::LineAnswers answers;
		answers.MainLine = "refs/heads/main";

		CHECK(Compute(builder, answers).Bases.empty());

		const Core::LineModel guessed = Compute(builder);

		CHECK(guessed.Bases.empty());
		CHECK_FALSE(guessed.MainLineGuessed);
	}
}

TEST_CASE("Lines: a main line answer naming a missing branch falls back to the guess")
{
	SUBCASE("a name no reference has")
	{
		GraphBuilder builder;
		AddLocalRepository(builder);
		Core::LineAnswers answers;
		answers.MainLine = "refs/heads/deleted";

		const Core::LineModel model = Compute(builder, answers);

		REQUIRE_FALSE(model.Bases.empty());
		CHECK(model.Bases[0].Ref == builder.RefIndex("refs/heads/main"));
		CHECK(model.Bases[0].Reason == Core::BaseReason::MainLineGuess);
		CHECK(model.MainLineGuessed);
	}

	SUBCASE("a reference whose tip the graph lacks")
	{
		GraphBuilder builder;
		AddLocalRepository(builder);
		builder.Ref("refs/heads/gone", "never-read");
		Core::LineAnswers answers;
		answers.MainLine = "refs/heads/gone";

		const Core::LineModel model = Compute(builder, answers);

		REQUIRE_FALSE(model.Bases.empty());
		CHECK(model.Bases[0].Ref == builder.RefIndex("refs/heads/main"));
		CHECK(model.Bases[0].Reason == Core::BaseReason::MainLineGuess);
		CHECK(model.MainLineGuessed);
	}
}

TEST_CASE("Lines: a default whose tip the graph lacks leaves the main line to the guess")
{
	GraphBuilder builder;
	AddLocalRepository(builder);
	builder.Ref("refs/remotes/origin/main", "never-read", true);

	const Core::LineModel model = Compute(builder);

	REQUIRE_FALSE(model.Bases.empty());
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/heads/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::MainLineGuess);
	CHECK(model.MainLineGuessed);
	CHECK_FALSE(IsBase(model, builder.RefIndex("refs/remotes/origin/main")));
}

TEST_CASE("Lines: the only default removed by the user leaves the guess to another reference")
{
	// origin/main shares main's commit and would speak for it; the user removed it, so main is guessed instead.
	GraphBuilder builder;
	AddLocalRepository(builder);
	builder.Ref("refs/remotes/origin/main", "m20", true);
	Core::LineAnswers answers;
	answers.RemovedBases = {"refs/remotes/origin/main"};

	const Core::LineModel model = Compute(builder, answers);

	REQUIRE_FALSE(model.Bases.empty());
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/heads/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::MainLineGuess);
	CHECK(model.MainLineGuessed);
	CHECK_FALSE(IsBase(model, builder.RefIndex("refs/remotes/origin/main")));
}

TEST_CASE("Lines: a merged branch left on the trunk does not take the guess from the trunk")
{
	// One person's repository: done was fast-forwarded into main at m60 and never deleted. feature/a and main are
	// both compared with done, but done is on main's line, and that line's top is main.
	GraphBuilder builder;
	builder.Chain("m", 70, std::nullopt, 1, {});
	builder.Commit("a1", {"m50"}, 71);
	builder.Ref("refs/heads/main", "m70");
	builder.Ref("refs/heads/done", "m60");
	builder.Ref("refs/heads/feature/a", "a1");
	builder.SetNow(80);

	const Core::LineModel model = Compute(builder);

	REQUIRE_FALSE(model.Bases.empty());
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/heads/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::MainLineGuess);
	CHECK(model.MainLineGuessed);
}

TEST_CASE("Lines: copies of one branch at one commit count once for the guess")
{
	// b1 and b2 fork from main's line; three copies of k sit at one commit forked from the orphan's line.
	GraphBuilder builder;
	builder.Chain("m", 10, std::nullopt, 1, {});
	builder.Commit("b1", {"m5"}, 5);
	builder.Commit("b2", {"m6"}, 6);
	builder.Chain("o", 5, std::nullopt, 1, {});
	builder.Commit("k1", {"o3"}, 3);
	builder.Ref("refs/heads/main", "m10");
	builder.Ref("refs/heads/b1", "b1");
	builder.Ref("refs/heads/b2", "b2");
	builder.Ref("refs/heads/orphan", "o5");
	builder.Ref("refs/heads/k", "k1");
	builder.Ref("refs/remotes/origin/k", "k1");
	builder.Ref("refs/remotes/fork/k", "k1");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);

	REQUIRE_FALSE(model.Bases.empty());
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/heads/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::MainLineGuess);
}

TEST_CASE("Lines: a local branch does not count its same-named upstream as what it is built on")
{
	// topic is one commit ahead of origin/topic on a side line from m5. Both are compared with main, so main's line
	// has two votes against one for other's line. Counted as built on its upstream, topic would vote for its own
	// line and three lines would tie, which other's line wins with the most authors.
	GraphBuilder builder;
	builder.Chain("m", 10, std::nullopt, 1, {"a@example.com", "b@example.com"});
	builder.Chain("s", 3, "m5", 11, {"a@example.com"});
	builder.Chain("o", 4, std::nullopt, 1, {"c@example.com", "d@example.com", "e@example.com", "f@example.com"});
	builder.Commit("k1", {"o2"}, 5, "c@example.com");
	builder.Ref("refs/heads/main", "m10");
	builder.Ref("refs/remotes/origin/topic", "s2");
	builder.Ref("refs/heads/topic", "s3", false, "refs/remotes/origin/topic");
	builder.Ref("refs/heads/other", "o4");
	builder.Ref("refs/heads/other-fix", "k1");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);

	REQUIRE_FALSE(model.Bases.empty());
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/heads/main"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::MainLineGuess);
}

TEST_CASE("Lines: an added or kept base with no default does not stop the guess")
{
	SUBCASE("added")
	{
		GraphBuilder builder;
		AddLocalRepository(builder);
		Core::LineAnswers answers;
		answers.AddedBases = {"refs/heads/feature/y"};

		const Core::LineModel model = Compute(builder, answers);

		REQUIRE(model.Bases.size() == 2);
		CHECK(model.Bases[0].Ref == builder.RefIndex("refs/heads/main"));
		CHECK(model.Bases[0].Reason == Core::BaseReason::MainLineGuess);
		CHECK(model.Bases[1].Ref == builder.RefIndex("refs/heads/feature/y"));
		CHECK(model.Bases[1].Reason == Core::BaseReason::AddedByUser);
		CHECK(model.MainLineGuessed);
	}

	SUBCASE("kept")
	{
		GraphBuilder builder;
		AddLocalRepository(builder);
		Core::LineAnswers answers;
		answers.KeptBases = {"refs/heads/feature/x"};

		const Core::LineModel model = Compute(builder, answers);

		REQUIRE(model.Bases.size() == 2);
		CHECK(model.Bases[0].Ref == builder.RefIndex("refs/heads/main"));
		CHECK(model.Bases[0].Reason == Core::BaseReason::MainLineGuess);
		CHECK(model.Bases[1].Ref == builder.RefIndex("refs/heads/feature/x"));
		CHECK(model.Bases[1].Reason == Core::BaseReason::Kept);
		CHECK(model.MainLineGuessed);
	}
}

TEST_CASE("Lines: a default is not an old copy of a local base built on its tip")
{
	GraphBuilder builder;
	AddDevelop(builder, 150);
	builder.Commit("s1", {"d150"}, 151);
	builder.Ref("refs/remotes/origin/develop", "d150", true);
	builder.Ref("refs/heads/staging", "s1");
	builder.SetNow(160);
	Core::LineAnswers answers;
	answers.AddedBases = {"refs/heads/staging"};

	const Core::LineModel model = Compute(builder, answers);

	REQUIRE(model.Bases.size() == 2);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/origin/develop"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::ServerDefault);
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/heads/staging"));
	CHECK(model.OldCopies.empty());
}

TEST_CASE("Lines: a default merged into another base as a second parent is not its old copy")
{
	GraphBuilder builder;
	AddDevelop(builder, 30);
	AddGitflowMaster(builder, 0);
	builder.Commit("M4", {"M3", "d30"}, 31);
	builder.Ref("refs/remotes/origin/develop", "d30", true);
	builder.Ref("refs/remotes/origin/master", "M4");
	builder.SetNow(40);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Bases.size() == 2);
	CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/origin/develop"));
	CHECK(model.Bases[0].Reason == Core::BaseReason::ServerDefault);
	CHECK(model.Bases[1].Ref == builder.RefIndex("refs/remotes/origin/master"));
	CHECK(model.Bases[1].Reason == Core::BaseReason::MergedLines);
	CHECK(model.OldCopies.empty());
}

TEST_CASE("Lines: of two defaults at one commit the remote with fewer branches holds the old copy")
{
	SUBCASE("upstream has more branches")
	{
		GraphBuilder builder;
		builder.Chain("u", 10, std::nullopt, 1, {});
		builder.Commit("f1", {"u5"}, 11);
		builder.Ref("refs/remotes/origin/main", "u10", true);
		builder.Ref("refs/remotes/upstream/main", "u10", true);
		builder.Ref("refs/remotes/upstream/feature", "f1");
		builder.SetNow(20);

		const Core::LineModel model = Compute(builder);

		REQUIRE(model.Bases.size() == 1);
		CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/upstream/main"));
		REQUIRE(model.OldCopies.size() == 1);
		CHECK(model.OldCopies[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
		CHECK(model.OldCopies[0].Of == builder.RefIndex("refs/remotes/upstream/main"));
	}

	SUBCASE("as many branches on each and the later remote name holds it")
	{
		GraphBuilder builder;
		builder.Chain("u", 10, std::nullopt, 1, {});
		builder.Ref("refs/remotes/upstream/main", "u10", true);
		builder.Ref("refs/remotes/origin/main", "u10", true);
		builder.SetNow(20);

		const Core::LineModel model = Compute(builder);

		REQUIRE(model.Bases.size() == 1);
		CHECK(model.Bases[0].Ref == builder.RefIndex("refs/remotes/origin/main"));
		REQUIRE(model.OldCopies.size() == 1);
		CHECK(model.OldCopies[0].Ref == builder.RefIndex("refs/remotes/upstream/main"));
		CHECK(model.OldCopies[0].Of == builder.RefIndex("refs/remotes/origin/main"));
	}
}
