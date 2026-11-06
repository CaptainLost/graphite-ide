#include <doctest/doctest.h>

#include "Core/Git/Content/ContentCheck.h"
#include "Core/Git/Content/ContentThresholds.h"
#include "Core/Git/PatchMatch.h"
#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

	const std::string kBase = "1\n2\n3\n4\n5\n6\n7\n8\n9\n";
	const std::string kLineTwoEdit = "1\ntwo\n3\n4\n5\n6\n7\n8\n9\n";
	const std::string kLinesTwoAndEightEdit = "1\ntwo\n3\n4\n5\n6\n7\neight\n9\n";
	const std::string kLinesTwoFiveAndEightEdit = "1\ntwo\n3\n4\nfive\n6\n7\neight\n9\n";
	const std::string kLineTwoEditedAgain = "1\nTWO\n3\n4\n5\n6\n7\neight\n9\n";

	constexpr std::size_t kPlentyOfBudget = 1000;

	Core::Oid OidOf(const std::string& hex)
	{
		const std::optional<Core::Oid> id = Core::Oid::FromHex(hex);
		REQUIRE(id.has_value());
		return *id;
	}

	// The id of no object in the repository: every byte of a real commit's id turned over.
	std::string MissingCommitHex(const std::string& realCommitHex)
	{
		Core::Oid missing = OidOf(realCommitHex);

		for (std::uint8_t& byte : missing.Bytes)
		{
			byte = static_cast<std::uint8_t>(~byte);
		}

		return missing.ToHex();
	}

	Core::Repository OpenInMemory(const GitFixture& fixture)
	{
		Core::Result<Core::Repository> opened = Core::Repository::Open(fixture.GetPath(), Core::ObjectWrites::InMemory);
		REQUIRE(opened.HasValue());
		return std::move(opened).GetValue();
	}

	// master holds a.txt as kBase; topic is branched from it and left checked out.
	void StartTopic(GitFixture& fixture)
	{
		fixture.Write("a.txt", kBase);
		fixture.Commit("base");
		fixture.Branch("topic");
		fixture.Checkout("topic");
	}

	Core::ContentVerdict Check(const GitFixture& fixture, const std::string& branchTip, const std::string& lineTip)
	{
		const Core::Repository repository = OpenInMemory(fixture);
		Core::PatchIdCache cache;

		const Core::Result<Core::ContentVerdict> verdict =
			Core::CheckMergedByContent(repository, OidOf(branchTip), OidOf(lineTip), cache);

		REQUIRE(verdict.HasValue());
		return verdict.GetValue();
	}

	struct SquashedTopic
	{
		std::string TopicTip;
		std::string Squash;
	};

	// topic edits lines two and four of a.txt in two commits. master then edits line eight
	// `lineEdits` times, each a commit that merging topic into would change, and then takes topic's
	// two edits in one squash commit.
	SquashedTopic SquashAfterLineEdits(GitFixture& fixture, std::size_t lineEdits)
	{
		StartTopic(fixture);
		fixture.Write("a.txt", "1\ntwo\n3\n4\n5\n6\n7\n8\n9\n");
		fixture.Commit("topic edits line two");
		fixture.Write("a.txt", "1\ntwo\n3\nfour\n5\n6\n7\n8\n9\n");
		const std::string topicTip = fixture.Commit("topic edits line four");

		fixture.Checkout("master");
		std::string lineEight = "8";

		for (std::size_t editNumber = 1; editNumber <= lineEdits; ++editNumber)
		{
			lineEight = std::format("eight {}", editNumber);
			fixture.Write("a.txt", std::format("1\n2\n3\n4\n5\n6\n7\n{}\n9\n", lineEight));
			fixture.Commit(std::format("master edits line eight, time {}", editNumber));
		}

		fixture.Write("a.txt", std::format("1\ntwo\n3\nfour\n5\n6\n7\n{}\n9\n", lineEight));
		const std::string squash = fixture.Commit("squash of topic");

		return SquashedTopic{.TopicTip = topicTip, .Squash = squash};
	}
}

TEST_CASE("Content: a branch squashed into its line is merged by a squash with its landing")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kLineTwoEdit);
	fixture.Commit("topic edits line two");
	fixture.Write("a.txt", kLinesTwoAndEightEdit);
	const std::string topicTip = fixture.Commit("topic edits line eight");

	// The commit after the squash holds topic's changes too, so the landing is the oldest that does.
	fixture.Checkout("master");
	fixture.Write("b.txt", "unrelated\n");
	fixture.Commit("unrelated");
	fixture.Write("a.txt", kLinesTwoAndEightEdit);
	const std::string squash = fixture.Commit("squash of topic");
	fixture.Write("a.txt", kLinesTwoFiveAndEightEdit);
	const std::string masterTip = fixture.Commit("master edits line five");

	const Core::ContentVerdict expected{
		.Outcome = Core::ContentOutcome::Merged,
		.Kind = Core::ContentKind::Squash,
		.Landing = OidOf(squash),
	};

	CHECK(Check(fixture, topicTip, masterTip) == expected);
}

TEST_CASE("Content: a branch rebased onto its line is merged by a rebase with each copy")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kLineTwoEdit);
	const std::string topicFirst = fixture.Commit("topic edits line two");
	fixture.Write("a.txt", kLinesTwoAndEightEdit);
	const std::string topicSecond = fixture.Commit("topic edits line eight");

	fixture.Checkout("master");
	fixture.Write("b.txt", "unrelated\n");
	fixture.Commit("unrelated");
	fixture.Write("a.txt", kLineTwoEdit);
	const std::string copyOfFirst = fixture.Commit("topic edits line two, rebased");
	fixture.Write("a.txt", kLinesTwoAndEightEdit);
	const std::string copyOfSecond = fixture.Commit("topic edits line eight, rebased");

	const Core::ContentVerdict expected{
		.Outcome = Core::ContentOutcome::Merged,
		.Kind = Core::ContentKind::Rebase,
		.Twins = {
			Core::PatchTwin{.Own = OidOf(topicSecond), .Twin = OidOf(copyOfSecond)},
			Core::PatchTwin{.Own = OidOf(topicFirst), .Twin = OidOf(copyOfFirst)},
		},
	};

	CHECK(Check(fixture, topicSecond, copyOfSecond) == expected);
}

TEST_CASE("Content: a cherry-picked branch whose line later changed the same lines is merged by a rebase")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kLineTwoEdit);
	fixture.Commit("topic edits line two");
	fixture.Write("a.txt", kLinesTwoAndEightEdit);
	const std::string topicTip = fixture.Commit("topic edits line eight");

	fixture.Checkout("master");
	fixture.Write("a.txt", kLineTwoEdit);
	fixture.Commit("topic edits line two, picked");
	fixture.Write("a.txt", kLinesTwoAndEightEdit);
	fixture.Commit("topic edits line eight, picked");
	fixture.Write("a.txt", kLineTwoEditedAgain);
	const std::string masterTip = fixture.Commit("master edits line two again");

	const Core::ContentVerdict verdict = Check(fixture, topicTip, masterTip);

	CHECK(verdict.Outcome == Core::ContentOutcome::Merged);
	CHECK(verdict.Kind == Core::ContentKind::Rebase);
	CHECK(verdict.Twins.size() == 2);
}

TEST_CASE("Content: a branch with one change not on the line is not merged")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kLineTwoEdit);
	fixture.Commit("topic edits line two");
	fixture.Write("a.txt", kLinesTwoAndEightEdit);
	const std::string topicTip = fixture.Commit("topic edits line eight");

	fixture.Checkout("master");
	fixture.Write("a.txt", kLineTwoEdit);
	const std::string masterTip = fixture.Commit("topic edits line two, picked");

	CHECK(Check(fixture, topicTip, masterTip) == Core::ContentVerdict{.Outcome = Core::ContentOutcome::NotMerged});
}

TEST_CASE("Content: a branch whose changes cancel out is not merged")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kBase + "10\n");
	fixture.Commit("topic adds a line");
	fixture.Write("a.txt", kBase);
	const std::string topicTip = fixture.Commit("topic removes the line again");

	fixture.Checkout("master");
	fixture.Write("b.txt", "unrelated\n");
	const std::string masterTip = fixture.Commit("unrelated");

	CHECK(Check(fixture, topicTip, masterTip) == Core::ContentVerdict{.Outcome = Core::ContentOutcome::NotMerged});
}

TEST_CASE("Content: a squash past the landing tries is merged without a landing")
{
	GitFixture fixture;
	const SquashedTopic history = SquashAfterLineEdits(fixture, Core::kLandingTries + 1);

	const Core::ContentVerdict expected{
		.Outcome = Core::ContentOutcome::Merged,
		.Kind = Core::ContentKind::Squash,
		.Landing = std::nullopt,
	};

	CHECK(Check(fixture, history.TopicTip, history.Squash) == expected);
}

TEST_CASE("Content: a squash at the last landing try names its landing")
{
	GitFixture fixture;
	const SquashedTopic history = SquashAfterLineEdits(fixture, Core::kLandingTries - 1);

	const Core::ContentVerdict verdict = Check(fixture, history.TopicTip, history.Squash);

	CHECK(verdict.Outcome == Core::ContentOutcome::Merged);
	CHECK(verdict.Kind == Core::ContentKind::Squash);
	CHECK(verdict.Landing == std::optional<Core::Oid>{OidOf(history.Squash)});
}

TEST_CASE("Content: a missing blob in the line's patches answers unknown")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kLineTwoEdit);
	const std::string topicTip = fixture.Commit("topic edits line two");

	// The merge needs no blob of c.txt, which only the line changed; the line's patch does.
	const std::string lineOnlyText = "only on the line\n";
	fixture.Checkout("master");
	fixture.Write("c.txt", lineOnlyText);
	const std::string masterTip = fixture.Commit("master adds c");
	fixture.RemoveObject(BlobHexOf(lineOnlyText));

	CHECK(Check(fixture, topicTip, masterTip) == Core::ContentVerdict{.Outcome = Core::ContentOutcome::Unknown});
}

TEST_CASE("Content: a tip the repository does not hold answers unknown")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kLineTwoEdit);
	const std::string topicTip = fixture.Commit("topic edits line two");
	fixture.Checkout("master");
	fixture.Write("b.txt", "unrelated\n");
	const std::string masterTip = fixture.Commit("unrelated");

	const Core::ContentVerdict unknown{.Outcome = Core::ContentOutcome::Unknown};

	CHECK(Check(fixture, MissingCommitHex(topicTip), masterTip) == unknown);
	CHECK(Check(fixture, topicTip, MissingCommitHex(masterTip)) == unknown);
}

TEST_CASE("Content: a commit's twin is found on a release line")
{
	GitFixture fixture;
	fixture.Write("a.txt", kBase);
	fixture.Commit("base");
	fixture.Branch("release");
	fixture.Write("a.txt", kLineTwoEdit);
	const std::string fix = fixture.Commit("fix on master");

	fixture.Checkout("release");
	fixture.Write("b.txt", "release only\n");
	fixture.Commit("release prepares");
	fixture.Write("a.txt", kLineTwoEdit);
	const std::string pick = fixture.Commit("fix picked onto release");
	fixture.Write("c.txt", "after the pick\n");
	const std::string releaseTip = fixture.Commit("release goes on");

	const Core::Repository repository = OpenInMemory(fixture);
	Core::PatchIdCache cache;

	const Core::Result<std::optional<Core::Oid>> twin =
		repository.FindPatchTwin(OidOf(fix), OidOf(releaseTip), kPlentyOfBudget, cache);

	REQUIRE(twin.HasValue());
	CHECK(twin.GetValue() == std::optional<Core::Oid>{OidOf(pick)});

	const std::size_t noBudget = 0;
	const Core::Result<std::optional<Core::Oid>> withoutBudget =
		repository.FindPatchTwin(OidOf(fix), OidOf(releaseTip), noBudget, cache);

	REQUIRE(withoutBudget.HasValue());
	CHECK_FALSE(withoutBudget.GetValue().has_value());
}
