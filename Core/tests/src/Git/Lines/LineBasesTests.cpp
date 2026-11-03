#include <doctest/doctest.h>

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineAnswers.h"
#include "Core/Git/Lines/LineMeasure.h"
#include "Core/Git/Lines/LineModel.h"

#include "Git/Lines/GraphBuilder.h"

#include <cstddef>
#include <optional>
#include <string>

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

	// main m1..m20 by three authors; release r1..r6 from m10 by three others over eight days, with no merges.
	void AddSquashOnlyRelease(GraphBuilder& builder)
	{
		builder.Chain("m", 20, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
		builder.Commit("r1", {"m10"}, 11, "p@example.com");
		builder.Commit("r2", {"r1"}, 12, "q@example.com");
		builder.Commit("r3", {"r2"}, 14, "r@example.com");
		builder.Commit("r4", {"r3"}, 15, "p@example.com");
		builder.Commit("r5", {"r4"}, 17, "q@example.com");
		builder.Commit("r6", {"r5"}, 19, "r@example.com");
		builder.Ref("refs/remotes/origin/main", "m20", true);
		builder.Ref("refs/remotes/origin/release/2", "r6");
	}

	// upstream's main u1..u30 by three authors; the fork's origin/main stopped at u5.
	void AddFork(GraphBuilder& builder, bool upstreamReportsDefault)
	{
		builder.Chain("u", 30, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
		builder.Ref("refs/remotes/origin/main", "u5", true);
		builder.Ref("refs/remotes/upstream/main", "u30", upstreamReportsDefault);
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
	CHECK_FALSE(IsBase(model, builder.RefIndex("refs/remotes/origin/cp")));
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
