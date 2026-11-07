#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Content/ContentCheck.h"
#include "Core/Git/Content/ContentThresholds.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"
#include "Core/Git/PatchMatch.h"
#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <git2.h>

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
	const std::string kLineEightEdit = "1\n2\n3\n4\n5\n6\n7\neight\n9\n";
	const std::string kLinesTwoAndFourEdit = "1\ntwo\n3\nfour\n5\n6\n7\n8\n9\n";

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

	Core::ContentVerdict CheckWithin(const GitFixture& fixture, const std::string& branchTip, const std::string& lineTip,
									 Core::PatchIdCache& cache, const Core::ContentBudgets& budgets)
	{
		const Core::Repository repository = OpenInMemory(fixture);

		const Core::Result<Core::ContentVerdict> verdict =
			Core::CheckMergedByContent(repository, OidOf(branchTip), OidOf(lineTip), cache, budgets);

		REQUIRE(verdict.HasValue());
		return verdict.GetValue();
	}

	// Patch ids are read while every blob is there, so a later check finds them all in the cache.
	void LearnPatchIds(const GitFixture& fixture, const std::string& branchTip, const std::string& lineTip,
					   Core::PatchIdCache& cache)
	{
		const Core::Repository repository = OpenInMemory(fixture);
		const Core::Result<Core::PatchMatch> match =
			repository.MatchPatches(OidOf(branchTip), OidOf(lineTip), kPlentyOfBudget, cache);

		REQUIRE(match.HasValue());
		REQUIRE(match.GetValue().Complete);
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
	// The squash is candidate kLandingTries + 1, one past the last try.
	GitFixture fixture;
	const SquashedTopic history = SquashAfterLineEdits(fixture, Core::kLandingTries);

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

TEST_CASE("Content: the landing search reads the branch's paths as written, not as patterns")
{
	// The search keeps each line commit's diff to the branch's paths: a name a pattern would read otherwise, "[ab]" as a
	// class or a leading "!" as an exclusion, must still name the file itself.
	std::string name;

	SUBCASE("brackets")
	{
		name = "f[ab].txt";
	}

	SUBCASE("a leading exclamation mark")
	{
		name = "!b.txt";
	}

	GitFixture fixture;
	fixture.Write(name, kBase);
	fixture.Commit("base");
	fixture.Branch("topic");
	fixture.Checkout("topic");
	fixture.Write(name, kLineTwoEdit);
	fixture.Commit("topic edits line two");
	fixture.Write(name, kLinesTwoAndFourEdit);
	const std::string topicTip = fixture.Commit("topic edits line four");

	fixture.Checkout("master");
	fixture.Write("fa.txt", "unrelated\n");
	fixture.Commit("unrelated");
	fixture.Write(name, kLinesTwoAndFourEdit);
	const std::string squash = fixture.Commit("squash of topic");

	const Core::ContentVerdict verdict = Check(fixture, topicTip, squash);

	CHECK(verdict.Outcome == Core::ContentOutcome::Merged);
	CHECK(verdict.Kind == Core::ContentKind::Squash);
	CHECK(verdict.Landing == std::optional<Core::Oid>{OidOf(squash)});
}

TEST_CASE("Content: a line commit's diff kept to the branch's paths takes them as written, not as patterns")
{
	// As a pattern, "f[ab].txt" names fa.txt; taken as written it names only a file called f[ab].txt.
	GitFixture fixture;
	fixture.Write("fa.txt", kBase);
	fixture.Commit("base");
	fixture.Write("fa.txt", kLineTwoEdit);
	const std::string editsFa = fixture.Commit("edit fa.txt");

	git_repository* handle = nullptr;
	REQUIRE(git_repository_open(&handle, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);

	std::size_t deltas = 0;
	bool diffed = false;

	{
		const Core::Result<Core::CommitHandle> commit = Core::LookupCommit(handle, OidOf(editsFa));

		if (commit.HasValue())
		{
			const Core::Result<Core::DiffHandle> diff =
				Core::DiffWithFirstParentOn(handle, commit.GetValue().get(), std::vector<std::string>{"f[ab].txt"});
			diffed = diff.HasValue();
			deltas = diffed ? git_diff_num_deltas(diff.GetValue().get()) : 0;
		}
	}

	git_repository_free(handle);

	REQUIRE(diffed);
	CHECK(deltas == 0);
}

TEST_CASE("Content: a squash past the landing chain budget is merged without a landing")
{
	// The line's first-parent chain since the fork is the edits and then the squash.
	const std::size_t lineEdits = 2;
	GitFixture fixture;
	const SquashedTopic history = SquashAfterLineEdits(fixture, lineEdits);
	Core::PatchIdCache cache;
	const Core::ContentBudgets oneShortOfTheSquash{.LandingChain = lineEdits};

	const Core::ContentVerdict expected{
		.Outcome = Core::ContentOutcome::Merged,
		.Kind = Core::ContentKind::Squash,
		.Landing = std::nullopt,
	};

	CHECK(CheckWithin(fixture, history.TopicTip, history.Squash, cache, oneShortOfTheSquash) == expected);
}

TEST_CASE("Content: a squash within the landing chain budget names its landing")
{
	const std::size_t lineEdits = 2;
	GitFixture fixture;
	const SquashedTopic history = SquashAfterLineEdits(fixture, lineEdits);
	Core::PatchIdCache cache;
	const Core::ContentBudgets reachingTheSquash{.LandingChain = lineEdits + 1};

	const Core::ContentVerdict verdict = CheckWithin(fixture, history.TopicTip, history.Squash, cache, reachingTheSquash);

	CHECK(verdict.Landing == std::optional<Core::Oid>{OidOf(history.Squash)});
}

TEST_CASE("Content: the landing search stops where the fork's history meets the line off its first-parent chain")
{
	// topic forks from side, which master merges as a second parent, so the fork is not on master's
	// first-parent chain. The chain since the fork is the squash, the merge and master's own commit;
	// the base commit below them is history the fork already has.
	GitFixture fixture;
	fixture.Write("a.txt", kBase);
	fixture.Commit("base");
	fixture.Branch("side");
	fixture.Checkout("side");
	fixture.Write("s.txt", "side\n");
	fixture.Commit("side adds s");
	fixture.Branch("topic");
	fixture.Checkout("topic");
	fixture.Write("a.txt", kLineTwoEdit);
	fixture.Commit("topic edits line two");
	fixture.Write("a.txt", kLinesTwoAndFourEdit);
	const std::string topicTip = fixture.Commit("topic edits line four");

	fixture.Checkout("master");
	fixture.Write("b.txt", "master\n");
	fixture.Commit("master adds b");
	fixture.MergeCommit("master merges side", "side");
	fixture.Write("a.txt", kLinesTwoAndFourEdit);
	const std::string squash = fixture.Commit("squash of topic");

	const std::size_t chainSinceTheFork = 3;
	Core::PatchIdCache cache;
	const Core::ContentBudgets justTheChain{.LandingChain = chainSinceTheFork};

	const Core::ContentVerdict verdict = CheckWithin(fixture, topicTip, squash, cache, justTheChain);

	CHECK(verdict.Outcome == Core::ContentOutcome::Merged);
	CHECK(verdict.Kind == Core::ContentKind::Squash);
	CHECK(verdict.Landing == std::optional<Core::Oid>{OidOf(squash)});
}

TEST_CASE("Content: a line past the patch budget answers unknown")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kLineTwoEdit);
	const std::string topicTip = fixture.Commit("topic edits line two");

	fixture.Checkout("master");
	fixture.Write("b.txt", "one\n");
	fixture.Commit("master adds b");
	fixture.Write("c.txt", "two\n");
	const std::string masterTip = fixture.Commit("master adds c");

	Core::PatchIdCache cache;
	const Core::ContentBudgets oneLinePatch{.LinePatches = 1};

	CHECK(CheckWithin(fixture, topicTip, masterTip, cache, oneLinePatch) ==
		  Core::ContentVerdict{.Outcome = Core::ContentOutcome::Unknown});
}

TEST_CASE("Content: branches with no common history are not merged")
{
	GitFixture fixture;
	fixture.Write("a.txt", kBase);
	const std::string masterTip = fixture.Commit("base");
	fixture.StartOrphan("topic");
	fixture.Write("b.txt", "a history of its own\n");
	const std::string topicTip = fixture.Commit("topic starts anew");

	CHECK(Check(fixture, topicTip, masterTip) == Core::ContentVerdict{.Outcome = Core::ContentOutcome::NotMerged});
}

TEST_CASE("Content: a squash whose line's patches miss a blob is merged by a squash")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kLineTwoEdit);
	const std::string topicTip = fixture.Commit("topic edits line two");

	// The squash also adds c.txt, whose blob its patch needs and the merge does not.
	const std::string lineOnlyText = "only on the line\n";
	fixture.Checkout("master");
	fixture.Write("a.txt", kLineTwoEdit);
	fixture.Write("c.txt", lineOnlyText);
	const std::string squash = fixture.Commit("squash of topic, and a file of the line's own");
	fixture.RemoveObject(BlobHexOf(lineOnlyText));

	const Core::ContentVerdict expected{
		.Outcome = Core::ContentOutcome::Merged,
		.Kind = Core::ContentKind::Squash,
		.Landing = OidOf(squash),
	};

	CHECK(Check(fixture, topicTip, squash) == expected);
}

TEST_CASE("Content: an unknown merge with a complete patch walk answers unknown")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kLineTwoEdit);
	const std::string topicTip = fixture.Commit("topic edits line two");

	// Both sides edit a.txt, so the merge needs topic's blob; the patch walk needs none, its ids known.
	fixture.Checkout("master");
	fixture.Write("a.txt", kLineEightEdit);
	const std::string masterTip = fixture.Commit("master edits line eight");

	Core::PatchIdCache cache;
	LearnPatchIds(fixture, topicTip, masterTip, cache);
	fixture.RemoveObject(BlobHexOf(kLineTwoEdit));

	CHECK(CheckWithin(fixture, topicTip, masterTip, cache, Core::ContentBudgets{}) ==
		  Core::ContentVerdict{.Outcome = Core::ContentOutcome::Unknown});
}

TEST_CASE("Content: a check on a repository that is not open fails as not open")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kLineTwoEdit);
	const std::string topicTip = fixture.Commit("topic edits line two");
	fixture.Checkout("master");
	fixture.Write("b.txt", "unrelated\n");
	const std::string masterTip = fixture.Commit("unrelated");

	Core::Repository movedFrom = OpenInMemory(fixture);
	const Core::Repository owner = std::move(movedFrom);
	Core::PatchIdCache cache;

	const Core::Result<Core::ContentVerdict> verdict =
		Core::CheckMergedByContent(movedFrom, OidOf(topicTip), OidOf(masterTip), cache);

	REQUIRE_FALSE(verdict.HasValue());
	CHECK(verdict.GetError().Message == Core::RepositoryNotOpen().Message);
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

	const Core::Result<Core::PatchTwinSearch> twin =
		repository.FindPatchTwin(OidOf(fix), OidOf(releaseTip), kPlentyOfBudget, cache);

	REQUIRE(twin.HasValue());
	CHECK(twin.GetValue() == Core::PatchTwinSearch{.Twin = OidOf(pick), .MoreToRead = false});

	const std::size_t noBudget = 0;
	const Core::Result<Core::PatchTwinSearch> withoutBudget =
		repository.FindPatchTwin(OidOf(fix), OidOf(releaseTip), noBudget, cache);

	REQUIRE(withoutBudget.HasValue());
	CHECK(withoutBudget.GetValue() == Core::PatchTwinSearch{.Twin = std::nullopt, .MoreToRead = true});

	// The release tip is read first; the pick is the second commit, so a budget of one stops short of it.
	const Core::Result<Core::PatchTwinSearch> oneCommit = repository.FindPatchTwin(OidOf(fix), OidOf(releaseTip), 1, cache);

	REQUIRE(oneCommit.HasValue());
	CHECK(oneCommit.GetValue() == Core::PatchTwinSearch{.Twin = std::nullopt, .MoreToRead = true});
}

TEST_CASE("Content: a search that reads every line commit without a copy has nothing more to read")
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
	fixture.Write("c.txt", "release goes on\n");
	const std::string releaseTip = fixture.Commit("release goes on");

	const Core::Repository repository = OpenInMemory(fixture);
	Core::PatchIdCache cache;

	// Two line commits the fix lacks: a budget of exactly two reads them all.
	const Core::Result<Core::PatchTwinSearch> search = repository.FindPatchTwin(OidOf(fix), OidOf(releaseTip), 2, cache);

	REQUIRE(search.HasValue());
	CHECK(search.GetValue() == Core::PatchTwinSearch{});
}
