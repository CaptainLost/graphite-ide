#include <doctest/doctest.h>

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineAnswers.h"
#include "Core/Git/Lines/LineModel.h"

#include "Git/Lines/GraphBuilder.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

	std::optional<Core::BranchLine> FindBranch(const Core::LineModel& model, std::size_t ref)
	{
		for (const Core::BranchLine& branch : model.Branches)
		{
			if (branch.Ref == ref)
			{
				return branch;
			}
		}

		return std::nullopt;
	}

	Core::BranchLine RequireBranch(const Core::LineModel& model, std::size_t ref)
	{
		const std::optional<Core::BranchLine> branch = FindBranch(model, ref);
		REQUIRE(branch.has_value());
		return *branch;
	}

	// GraphBuilder::kNoRef when the branch is compared with nothing, so that a check against a ref fails.
	std::size_t ComparedRef(const Core::BranchLine& branch)
	{
		return branch.ComparedWith.value_or(GraphBuilder::kNoRef);
	}

	std::size_t RowRef(const Core::BranchLine& branch)
	{
		return branch.SameRowAs.value_or(GraphBuilder::kNoRef);
	}

	bool IsSameBranchLine(const Core::BranchLine& first, const Core::BranchLine& second)
	{
		return first.Ref == second.Ref && first.Kind == second.Kind && first.ComparedWith == second.ComparedWith
			&& first.ByAnswer == second.ByAnswer && first.State == second.State && first.Ahead == second.Ahead
			&& first.SameRowAs == second.SameRowAs;
	}

	// main m1..mN by three authors on days 1..N; origin/main is the default at its top.
	void AddTrunk(GraphBuilder& builder, int commitCount)
	{
		builder.Chain("m", commitCount, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
		builder.Ref("refs/remotes/origin/main", "m" + std::to_string(commitCount), true);
	}

	// release r1..r6 from m10 by three people over eight days, with no merges: a base by its authors.
	void AddRelease(GraphBuilder& builder)
	{
		builder.Commit("r1", {"m10"}, 11, "p@example.com");
		builder.Commit("r2", {"r1"}, 12, "q@example.com");
		builder.Commit("r3", {"r2"}, 14, "r@example.com");
		builder.Commit("r4", {"r3"}, 15, "p@example.com");
		builder.Commit("r5", {"r4"}, 17, "q@example.com");
		builder.Commit("r6", {"r5"}, 19, "r@example.com");
		builder.Ref("refs/remotes/origin/release/2", "r6");
	}

	constexpr int kLongTrunkLength = 50000;
	constexpr int kTopicCount = 2000;
	constexpr int kTopicForkSpacing = kLongTrunkLength / kTopicCount;
	constexpr int kEarlyForkNumber = 10;
	constexpr int kEarlyMergeNumber = kLongTrunkLength - 10;

	std::string TrunkCommit(int number)
	{
		return "m" + std::to_string(number);
	}

	std::string TopicCommit(int topic, int number)
	{
		return "t" + std::to_string(topic) + "-" + std::to_string(number);
	}

	std::string TopicRef(int topic)
	{
		return "refs/remotes/origin/topic/" + std::to_string(topic);
	}

	// main m1..m50000 by three authors; m49990 also merges early1, forked from m10. Topic k is two commits forked from
	// m(25k); stack is one commit on topic 1.
	void AddLongTrunkWithTopics(GraphBuilder& builder)
	{
		const std::vector<std::string_view> trunkAuthors{"a@example.com", "b@example.com", "c@example.com"};
		const std::string_view topicAuthor = "t@example.com";
		const std::vector<std::string_view> noParents;

		builder.Commit(TrunkCommit(1), noParents, 1, trunkAuthors[0]);

		for (int number = 2; number <= kLongTrunkLength; ++number)
		{
			const std::string previous = TrunkCommit(number - 1);
			std::vector<std::string_view> parents{previous};

			if (number == kEarlyMergeNumber)
			{
				parents.push_back("early1");
			}

			builder.Commit(TrunkCommit(number), parents, number, trunkAuthors[static_cast<std::size_t>(number) % trunkAuthors.size()]);
		}

		builder.Commit("early1", {TrunkCommit(kEarlyForkNumber)}, kEarlyForkNumber + 1, "e@example.com");

		for (int topic = 1; topic <= kTopicCount; ++topic)
		{
			const int forkNumber = topic * kTopicForkSpacing;
			builder.Commit(TopicCommit(topic, 1), {TrunkCommit(forkNumber)}, forkNumber + 1, topicAuthor);
			builder.Commit(TopicCommit(topic, 2), {TopicCommit(topic, 1)}, forkNumber + 2, topicAuthor);
			builder.Ref(TopicRef(topic), TopicCommit(topic, 2));
		}

		builder.Commit("s1", {TopicCommit(1, 2)}, kTopicForkSpacing + 3, topicAuthor);
		builder.Ref("refs/remotes/origin/main", TrunkCommit(kLongTrunkLength), true);
		builder.Ref("refs/remotes/origin/early", "early1");
		builder.Ref("refs/heads/stack", "s1");
		builder.SetNow(kLongTrunkLength + 10);
	}
}

TEST_CASE("Lines: a feature on the trunk is compared with the trunk and counts its own commits")
{
	GraphBuilder builder;
	AddTrunk(builder, 20);
	builder.Chain("a", 2, "m18", 21, {"d@example.com"});
	builder.Ref("refs/remotes/origin/feature/a", "a2");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);
	const Core::BranchLine feature = RequireBranch(model, builder.RefIndex("refs/remotes/origin/feature/a"));

	CHECK(feature.Kind == Core::ComparedKind::Base);
	CHECK(ComparedRef(feature) == builder.RefIndex("refs/remotes/origin/main"));
	CHECK_FALSE(feature.ByAnswer);
	CHECK(feature.State == Core::BranchState::NotMerged);
	CHECK(feature.Ahead == 2);
	CHECK_FALSE(feature.SameRowAs.has_value());
}

TEST_CASE("Lines: a branch merged by a merge commit is merged by history")
{
	GraphBuilder builder;
	builder.Chain("m", 5, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
	builder.Commit("b1", {"m5"}, 6, "d@example.com");
	builder.Commit("m6", {"m5", "b1"}, 7, "a@example.com");
	builder.Ref("refs/remotes/origin/main", "m6", true);
	builder.Ref("refs/remotes/origin/feature/b", "b1");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);
	const Core::BranchLine feature = RequireBranch(model, builder.RefIndex("refs/remotes/origin/feature/b"));

	CHECK(feature.Kind == Core::ComparedKind::Base);
	CHECK(ComparedRef(feature) == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(feature.State == Core::BranchState::MergedByHistory);
	CHECK(feature.Ahead == 0);
}

TEST_CASE("Lines: a branch just created on the trunk has no own commits and is not called merged")
{
	GraphBuilder builder;
	AddTrunk(builder, 20);
	builder.Ref("refs/heads/fresh", "m20");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);
	const Core::BranchLine fresh = RequireBranch(model, builder.RefIndex("refs/heads/fresh"));

	CHECK(fresh.Kind == Core::ComparedKind::Base);
	CHECK(ComparedRef(fresh) == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(fresh.State == Core::BranchState::NoOwnCommits);
	CHECK(fresh.Ahead == 0);
}

TEST_CASE("Lines: a local branch behind its same-named upstream sits in the upstream's row")
{
	GraphBuilder builder;
	AddTrunk(builder, 20);
	builder.Ref("refs/heads/main", "m14", false, "refs/remotes/origin/main");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);
	const Core::BranchLine main = RequireBranch(model, builder.RefIndex("refs/heads/main"));

	CHECK(RowRef(main) == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(main.Kind == Core::ComparedKind::Base);
	CHECK(ComparedRef(main) == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(main.State == Core::BranchState::NoOwnCommits);
	CHECK(main.Ahead == 0);
}

TEST_CASE("Lines: a local branch tracking a differently named upstream keeps its own row")
{
	GraphBuilder builder;
	AddTrunk(builder, 20);
	builder.Commit("f1", {"m20"}, 21, "d@example.com");
	builder.Ref("refs/heads/fix", "f1", false, "refs/remotes/origin/main");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);
	const Core::BranchLine fix = RequireBranch(model, builder.RefIndex("refs/heads/fix"));

	CHECK_FALSE(fix.SameRowAs.has_value());
	CHECK(fix.Kind == Core::ComparedKind::Base);
	CHECK(ComparedRef(fix) == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(fix.State == Core::BranchState::NotMerged);
	CHECK(fix.Ahead == 1);
}

TEST_CASE("Lines: branches built on a branch are compared with it")
{
	GraphBuilder builder;
	AddTrunk(builder, 20);
	builder.Chain("a", 3, "m10", 21, {"x@example.com"});
	builder.Chain("b", 2, "a3", 24, {"x@example.com"});
	builder.Commit("c1", {"a2"}, 26, "x@example.com");
	builder.Ref("refs/heads/a", "a3");
	builder.Ref("refs/heads/b", "b2");
	builder.Ref("refs/heads/c", "c1");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);
	const Core::BranchLine onTopOfA = RequireBranch(model, builder.RefIndex("refs/heads/b"));
	const Core::BranchLine forkedFromA = RequireBranch(model, builder.RefIndex("refs/heads/c"));
	const Core::BranchLine a = RequireBranch(model, builder.RefIndex("refs/heads/a"));

	CHECK(onTopOfA.Kind == Core::ComparedKind::Branch);
	CHECK(ComparedRef(onTopOfA) == builder.RefIndex("refs/heads/a"));
	CHECK(onTopOfA.State == Core::BranchState::NotMerged);
	CHECK(onTopOfA.Ahead == 2);

	CHECK(forkedFromA.Kind == Core::ComparedKind::Branch);
	CHECK(ComparedRef(forkedFromA) == builder.RefIndex("refs/heads/a"));
	CHECK(forkedFromA.State == Core::BranchState::NotMerged);
	CHECK(forkedFromA.Ahead == 1);

	CHECK(a.Kind == Core::ComparedKind::Base);
	CHECK(ComparedRef(a) == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(a.State == Core::BranchState::NotMerged);
	CHECK(a.Ahead == 3);
}

TEST_CASE("Lines: a fix forked from a release line is compared with the release line")
{
	GraphBuilder builder;
	AddTrunk(builder, 20);
	AddRelease(builder);
	builder.Commit("f1", {"r4"}, 16, "s@example.com");
	builder.Ref("refs/remotes/origin/fix", "f1");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);
	const Core::BranchLine fix = RequireBranch(model, builder.RefIndex("refs/remotes/origin/fix"));

	CHECK(fix.Kind == Core::ComparedKind::Base);
	CHECK(ComparedRef(fix) == builder.RefIndex("refs/remotes/origin/release/2"));
	CHECK(fix.State == Core::BranchState::NotMerged);
	CHECK(fix.Ahead == 1);
}

TEST_CASE("Lines: a fix forked just before the release went back into the trunk is still on the release line")
{
	GraphBuilder builder;
	builder.Chain("m", 20, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
	AddRelease(builder);
	builder.Commit("m21", {"m20", "r4"}, 21, "a@example.com");
	builder.Commit("f1", {"r4"}, 16, "s@example.com");
	builder.Ref("refs/remotes/origin/main", "m21", true);
	builder.Ref("refs/remotes/origin/fix", "f1");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);
	const Core::BranchLine fix = RequireBranch(model, builder.RefIndex("refs/remotes/origin/fix"));

	CHECK(fix.Kind == Core::ComparedKind::Base);
	CHECK(ComparedRef(fix) == builder.RefIndex("refs/remotes/origin/release/2"));
	CHECK(fix.State == Core::BranchState::NotMerged);
	CHECK(fix.Ahead == 1);
}

TEST_CASE("Lines: the user's comparison wins and an empty one leaves the branch on its own")
{
	GraphBuilder builder;
	AddTrunk(builder, 20);
	AddRelease(builder);
	builder.Chain("a", 2, "m18", 21, {"d@example.com"});
	builder.Ref("refs/remotes/origin/feature/a", "a2");
	builder.SetNow(30);
	const std::string featureName = "refs/remotes/origin/feature/a";
	Core::LineAnswers answers;

	SUBCASE("a comparison with the release line")
	{
		answers.Comparisons[featureName] = "refs/remotes/origin/release/2";

		const Core::LineModel model = Compute(builder, answers);
		const Core::BranchLine feature = RequireBranch(model, builder.RefIndex(featureName));

		CHECK(feature.ByAnswer);
		CHECK(feature.Kind == Core::ComparedKind::Base);
		CHECK(ComparedRef(feature) == builder.RefIndex("refs/remotes/origin/release/2"));
		CHECK(feature.State == Core::BranchState::NotMerged);
		CHECK(feature.Ahead == 10);
	}

	SUBCASE("an empty comparison")
	{
		answers.Comparisons[featureName] = std::nullopt;

		const Core::LineModel model = Compute(builder, answers);
		const Core::BranchLine feature = RequireBranch(model, builder.RefIndex(featureName));

		CHECK(feature.ByAnswer);
		CHECK(feature.Kind == Core::ComparedKind::None);
		CHECK_FALSE(feature.ComparedWith.has_value());
		CHECK(feature.State == Core::BranchState::NotCompared);
		CHECK(feature.Ahead == 0);
	}

	SUBCASE("a comparison with a branch that is no base")
	{
		builder.Ref("refs/heads/review", "a1");
		answers.Comparisons[featureName] = "refs/heads/review";

		const Core::LineModel model = Compute(builder, answers);
		const Core::BranchLine feature = RequireBranch(model, builder.RefIndex(featureName));

		CHECK(feature.ByAnswer);
		CHECK(feature.Kind == Core::ComparedKind::Branch);
		CHECK(ComparedRef(feature) == builder.RefIndex("refs/heads/review"));
		CHECK(feature.State == Core::BranchState::NotMerged);
		CHECK(feature.Ahead == 1);
	}

	SUBCASE("a comparison with a line whose first parents hold the branch's tip finds no own commits")
	{
		builder.Ref("refs/heads/old", "m5");
		answers.Comparisons["refs/heads/old"] = "refs/remotes/origin/release/2";

		const Core::LineModel model = Compute(builder, answers);
		const Core::BranchLine old = RequireBranch(model, builder.RefIndex("refs/heads/old"));

		CHECK(old.ByAnswer);
		CHECK(old.Kind == Core::ComparedKind::Base);
		CHECK(ComparedRef(old) == builder.RefIndex("refs/remotes/origin/release/2"));
		CHECK(old.State == Core::BranchState::NoOwnCommits);
		CHECK(old.Ahead == 0);
	}

	SUBCASE("a comparison naming no reference is ignored")
	{
		answers.Comparisons[featureName] = "refs/remotes/origin/deleted";

		const Core::LineModel model = Compute(builder, answers);
		const Core::BranchLine feature = RequireBranch(model, builder.RefIndex(featureName));

		CHECK_FALSE(feature.ByAnswer);
		CHECK(feature.Kind == Core::ComparedKind::Base);
		CHECK(ComparedRef(feature) == builder.RefIndex("refs/remotes/origin/main"));
		CHECK(feature.Ahead == 2);
	}

	SUBCASE("a comparison naming a reference whose tip the graph lacks is ignored")
	{
		builder.Ref("refs/heads/gone", "never-read");
		answers.Comparisons[featureName] = "refs/heads/gone";

		const Core::LineModel model = Compute(builder, answers);
		const Core::BranchLine feature = RequireBranch(model, builder.RefIndex(featureName));

		CHECK_FALSE(feature.ByAnswer);
		CHECK(feature.Kind == Core::ComparedKind::Base);
		CHECK(ComparedRef(feature) == builder.RefIndex("refs/remotes/origin/main"));
		CHECK(feature.Ahead == 2);
	}
}

TEST_CASE("Lines: an orphan branch has no common history and a shallow one is cut off")
{
	GraphBuilder builder;
	AddTrunk(builder, 20);
	builder.Chain("o", 2, std::nullopt, 21, {"d@example.com"});
	const std::vector<std::string_view> noParents;
	builder.Commit("s1", noParents, 22, "e@example.com");
	builder.Cut("s1");
	builder.Commit("s2", {"s1"}, 23, "e@example.com");
	builder.Ref("refs/remotes/origin/orphan", "o2");
	builder.Ref("refs/remotes/origin/shallow", "s2");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);
	const Core::BranchLine orphan = RequireBranch(model, builder.RefIndex("refs/remotes/origin/orphan"));
	const Core::BranchLine shallow = RequireBranch(model, builder.RefIndex("refs/remotes/origin/shallow"));

	CHECK(orphan.State == Core::BranchState::NoCommonHistory);
	CHECK(orphan.Kind == Core::ComparedKind::None);
	CHECK_FALSE(orphan.ComparedWith.has_value());
	CHECK(orphan.Ahead == 0);

	CHECK(shallow.State == Core::BranchState::HistoryCutOff);
	CHECK(shallow.Kind == Core::ComparedKind::None);
	CHECK_FALSE(shallow.ComparedWith.has_value());
	CHECK(shallow.Ahead == 0);
}

TEST_CASE("Lines: a reference whose tip is missing from the graph is reported and not dropped")
{
	GraphBuilder builder;
	AddTrunk(builder, 20);
	builder.Chain("a", 3, "m10", 21, {"x@example.com"});
	builder.Chain("b", 2, "a3", 24, {"x@example.com"});
	builder.Ref("refs/heads/a", "a3");
	builder.Ref("refs/heads/b", "b2");
	builder.SetNow(30);
	const Core::LineModel withoutMissing = Compute(builder);

	builder.Ref("refs/heads/gone", "never-read");
	Core::LineAnswers answers;
	answers.Comparisons["refs/heads/gone"] = "refs/remotes/origin/main";
	Core::LineModel withMissing;

	CHECK_NOTHROW(withMissing = Compute(builder, answers));

	const Core::BranchLine gone = RequireBranch(withMissing, builder.RefIndex("refs/heads/gone"));

	CHECK(gone.State == Core::BranchState::TipMissing);
	CHECK(gone.Kind == Core::ComparedKind::None);
	CHECK_FALSE(gone.ComparedWith.has_value());
	CHECK(gone.Ahead == 0);

	REQUIRE(withMissing.Bases.size() == withoutMissing.Bases.size());
	REQUIRE(withMissing.Branches.size() == withoutMissing.Branches.size() + 1);

	for (std::size_t position = 0; position < withoutMissing.Bases.size(); ++position)
	{
		CHECK(withMissing.Bases[position].Ref == withoutMissing.Bases[position].Ref);
	}

	for (std::size_t position = 0; position < withoutMissing.Branches.size(); ++position)
	{
		CHECK(IsSameBranchLine(withMissing.Branches[position], withoutMissing.Branches[position]));
	}
}

TEST_CASE("Lines: every reference that is not a base gets a branch line in input order")
{
	GraphBuilder builder;
	builder.Chain("m", 20, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
	builder.Commit("a1", {"m18"}, 21, "d@example.com");
	builder.Ref("refs/heads/feature", "a1");
	builder.Ref("refs/remotes/origin/main", "m20", true);
	builder.Ref("refs/heads/gone", "never-read");
	builder.Ref("refs/heads/fresh", "m20");
	builder.SetNow(30);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.Branches.size() == 3);
	CHECK(model.Branches[0].Ref == builder.RefIndex("refs/heads/feature"));
	CHECK(model.Branches[1].Ref == builder.RefIndex("refs/heads/gone"));
	CHECK(model.Branches[2].Ref == builder.RefIndex("refs/heads/fresh"));
}

TEST_CASE("Lines: a fork's stale default has no own commits on the line it is an old copy of")
{
	GraphBuilder builder;
	builder.Chain("u", 30, std::nullopt, 1, {"a@example.com", "b@example.com", "c@example.com"});
	builder.Ref("refs/remotes/origin/main", "u5", true);
	builder.Ref("refs/remotes/upstream/main", "u30", true);
	builder.SetNow(40);

	const Core::LineModel model = Compute(builder);

	REQUIRE(model.OldCopies.size() == 1);
	REQUIRE(model.Branches.size() == 1);

	const Core::BranchLine stale = RequireBranch(model, builder.RefIndex("refs/remotes/origin/main"));

	CHECK(stale.Kind == Core::ComparedKind::Base);
	CHECK(ComparedRef(stale) == builder.RefIndex("refs/remotes/upstream/main"));
	CHECK(stale.State == Core::BranchState::NoOwnCommits);
}

TEST_CASE("Lines: two thousand branches on a fifty thousand commit trunk are each counted without walking the trunk")
{
	GraphBuilder builder;
	AddLongTrunkWithTopics(builder);

	const Core::LineModel model = Compute(builder);

	CHECK(model.Branches.size() == kTopicCount + 2);

	const Core::BranchLine middleTopic = RequireBranch(model, builder.RefIndex(TopicRef(kTopicCount / 2)));
	const Core::BranchLine early = RequireBranch(model, builder.RefIndex("refs/remotes/origin/early"));
	const Core::BranchLine stack = RequireBranch(model, builder.RefIndex("refs/heads/stack"));

	CHECK(middleTopic.Kind == Core::ComparedKind::Base);
	CHECK(ComparedRef(middleTopic) == builder.RefIndex("refs/remotes/origin/main"));
	CHECK(middleTopic.State == Core::BranchState::NotMerged);
	CHECK(middleTopic.Ahead == 2);

	CHECK(early.Kind == Core::ComparedKind::Base);
	CHECK(early.State == Core::BranchState::MergedByHistory);
	CHECK(early.Ahead == 0);

	CHECK(stack.Kind == Core::ComparedKind::Branch);
	CHECK(ComparedRef(stack) == builder.RefIndex(TopicRef(1)));
	CHECK(stack.State == Core::BranchState::NotMerged);
	CHECK(stack.Ahead == 1);
}
