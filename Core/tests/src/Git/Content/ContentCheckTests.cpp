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
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <set>
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

	// A zlib stream that stores `data` in one uncompressed block: header, block, length and its complement, the data, and
	// the Adler-32 of the data.
	std::string ZlibStreamStoring(std::string_view data)
	{
		constexpr std::uint32_t kAdlerModulus = 65521;
		std::uint32_t low = 1;
		std::uint32_t high = 0;

		for (const char byte : data)
		{
			low = (low + static_cast<std::uint8_t>(byte)) % kAdlerModulus;
			high = (high + low) % kAdlerModulus;
		}

		const std::uint32_t adler = (high << 16) | low;
		const auto length = static_cast<std::uint16_t>(data.size());
		const auto complement = static_cast<std::uint16_t>(~length);

		std::string stream{'\x78', '\x01', '\x01'};
		stream += static_cast<char>(length & 0xFF);
		stream += static_cast<char>(length >> 8);
		stream += static_cast<char>(complement & 0xFF);
		stream += static_cast<char>(complement >> 8);
		stream += data;

		for (const int shift : {24, 16, 8, 0})
		{
			stream += static_cast<char>((adler >> shift) & 0xFF);
		}

		return stream;
	}

	bool TreesDifferAt(const Core::Repository& repository, const std::string& from, const std::string& to,
					   const std::vector<std::string>& paths)
	{
		const Core::Result<bool> differ = repository.TreesDifferAt(OidOf(from), OidOf(to), paths);
		REQUIRE(differ.HasValue());
		return differ.GetValue();
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

TEST_CASE("Content: a tip that cannot be read for another reason than a missing object fails the check")
{
	GitFixture fixture;
	StartTopic(fixture);
	fixture.Write("a.txt", kLineTwoEdit);
	const std::string topicTip = fixture.Commit("topic edits line two");
	fixture.Checkout("master");
	fixture.Write("b.txt", "unrelated\n");
	const std::string masterTip = fixture.Commit("unrelated");

	// The commit's file inflates, but to no object header: libgit2 fails to parse it (GIT_ERROR_OBJECT), which is not an
	// object the database lacks. A file that does not inflate reads as missing (GIT_ERROR_ODB), as ruled for a corrupt one.
	fixture.RemoveObject(topicTip);
	const std::filesystem::path objectFile = fixture.GetPath() / ".git" / "objects" / topicTip.substr(0, 2) / topicTip.substr(2);

	{
		std::ofstream unparsable{objectFile, std::ios::binary | std::ios::trunc};
		REQUIRE(unparsable.is_open());
		unparsable << ZlibStreamStoring("no header here");
	}

	const Core::Repository repository = OpenInMemory(fixture);
	Core::PatchIdCache cache;

	const Core::Result<bool> held = repository.HoldsCommit(OidOf(topicTip));
	REQUIRE_FALSE(held.HasValue());
	CHECK(held.GetError().Code == Core::ErrorCode::IoFailure);

	const Core::Result<Core::ContentVerdict> verdict = Core::CheckMergedByContent(repository, OidOf(topicTip), OidOf(masterTip), cache);
	REQUIRE_FALSE(verdict.HasValue());
	CHECK(verdict.GetError().Code == Core::ErrorCode::IoFailure);

	// A commit that is not there at all is still an answer.
	const Core::Result<bool> missing = repository.HoldsCommit(OidOf(MissingCommitHex(masterTip)));
	REQUIRE(missing.HasValue());
	CHECK_FALSE(missing.GetValue());
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

TEST_CASE("Content: the paths two commits' trees differ at are listed in order, from trees alone")
{
	GitFixture fixture;
	fixture.Write("a.txt", kBase);
	fixture.Write("dir/b.txt", kBase);
	fixture.Write("c.txt", kBase);
	const std::string older = fixture.Commit("base");

	// An edit in a folder, a deletion, an addition and a rename, which counts at both its paths.
	fixture.Write("dir/b.txt", kLineTwoEdit);
	fixture.Remove("c.txt");
	fixture.Write("d.txt", "new\n");
	fixture.Rename("a.txt", "e.txt");
	const std::string newer = fixture.Commit("changes");

	// No blob is read: the clone may lack the new ones.
	fixture.RemoveObject(BlobHexOf(kLineTwoEdit));
	fixture.RemoveObject(BlobHexOf("new\n"));

	const Core::Result<Core::Repository> repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	const Core::Result<std::vector<std::string>> paths = repository.GetValue().PathsChangedBetween(OidOf(older), OidOf(newer));
	REQUIRE(paths.HasValue());
	CHECK(paths.GetValue() == std::vector<std::string>{"a.txt", "c.txt", "d.txt", "dir/b.txt", "e.txt"});

	const Core::Result<std::vector<std::string>> same = repository.GetValue().PathsChangedBetween(OidOf(newer), OidOf(newer));
	REQUIRE(same.HasValue());
	CHECK(same.GetValue().empty());

	const Core::Result<std::vector<std::string>> missing =
		repository.GetValue().PathsChangedBetween(OidOf(older), OidOf(MissingCommitHex(newer)));
	REQUIRE_FALSE(missing.HasValue());
	CHECK(missing.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("Content: two commits' trees differ at a path only where it changed, the path taken as written")
{
	GitFixture fixture;
	fixture.Write("a.txt", kBase);
	fixture.Write("dir/b.txt", kBase);
	fixture.Write("fa.txt", kBase);
	const std::string fork = fixture.Commit("base");
	fixture.Write("dir/b.txt", kLineTwoEdit);
	fixture.Write("fa.txt", kLineEightEdit);
	const std::string tip = fixture.Commit("edits dir/b.txt and fa.txt");

	fixture.RemoveObject(BlobHexOf(kLineTwoEdit));
	fixture.RemoveObject(BlobHexOf(kLineEightEdit));

	const Core::Result<Core::Repository> opened = Core::Repository::Open(fixture.GetPath());
	REQUIRE(opened.HasValue());
	const Core::Repository& repository = opened.GetValue();

	CHECK(TreesDifferAt(repository, fork, tip, {"dir/b.txt"}));
	CHECK(TreesDifferAt(repository, fork, tip, {"a.txt", "fa.txt"}));
	CHECK(TreesDifferAt(repository, fork, tip, {"dir"}));

	CHECK_FALSE(TreesDifferAt(repository, fork, tip, {"a.txt"}));
	CHECK_FALSE(TreesDifferAt(repository, fork, tip, {}));

	// A pattern would name fa.txt, and a name's beginning would name dir/b.txt or dir: taken as written, they name
	// nothing that changed.
	CHECK_FALSE(TreesDifferAt(repository, fork, tip, {"f[ab].txt"}));
	CHECK_FALSE(TreesDifferAt(repository, fork, tip, {"dir/b"}));
	CHECK_FALSE(TreesDifferAt(repository, fork, tip, {"di"}));

	const Core::Result<bool> missing = repository.TreesDifferAt(OidOf(MissingCommitHex(fork)), OidOf(tip), {"a.txt"});
	REQUIRE_FALSE(missing.HasValue());
	CHECK(missing.GetError().Code == Core::ErrorCode::NotFound);
}

namespace {

	// libgit2's own diff of two commits' trees, kept to `paths` taken as written when there are any: the reference the
	// repository's walk of the trees must agree with.
	struct ReferenceDiff
	{
		std::set<std::string> Paths;
		bool Diffed = false;
	};

	ReferenceDiff DiffOfLibgit2(git_repository* handle, const std::string& olderHex, const std::string& newerHex,
								const std::vector<std::string>& paths)
	{
		ReferenceDiff reference;
		const Core::Result<Core::CommitHandle> older = Core::LookupCommit(handle, OidOf(olderHex));
		const Core::Result<Core::CommitHandle> newer = Core::LookupCommit(handle, OidOf(newerHex));
		Core::TreeHandle olderTree;
		Core::TreeHandle newerTree;

		if (!older.HasValue() || !newer.HasValue() || git_commit_tree(Core::Out(olderTree), older.GetValue().get()) != 0 ||
			git_commit_tree(Core::Out(newerTree), newer.GetValue().get()) != 0)
		{
			return reference;
		}

		std::vector<std::string> copies = paths;
		std::vector<char*> pathspec;

		for (std::string& path : copies)
		{
			pathspec.push_back(path.data());
		}

		git_diff_options options = GIT_DIFF_OPTIONS_INIT;
		options.flags |= GIT_DIFF_DISABLE_PATHSPEC_MATCH;
		options.pathspec.strings = pathspec.data();
		options.pathspec.count = pathspec.size();

		Core::DiffHandle diff;

		if (git_diff_tree_to_tree(Core::Out(diff), handle, olderTree.get(), newerTree.get(), &options) != 0)
		{
			return reference;
		}

		for (std::size_t index = 0; index < git_diff_num_deltas(diff.get()); ++index)
		{
			const git_diff_delta* delta = git_diff_get_delta(diff.get(), index);
			reference.Paths.insert(delta->old_file.path);
			reference.Paths.insert(delta->new_file.path);
		}

		reference.Diffed = true;
		return reference;
	}
}

TEST_CASE("Content: the walk of two trees finds what libgit2's diff of them finds")
{
	GitFixture fixture;
	fixture.Write("top.txt", kBase);
	fixture.Write("src/a/x.txt", kBase);
	fixture.Write("src/a/y.txt", kBase);
	fixture.Write("src/a-b.txt", kBase);
	fixture.Write("src/b/z.txt", kBase);
	fixture.Write("src/b/deep/w.txt", kBase);
	fixture.Write("docs/r.md", kBase);
	fixture.Write("kept/k.txt", kBase);
	const std::string older = fixture.Commit("base");

	// An edit, a folder gone, a folder new, a file turned into a folder, a rename, and the name sorting between a folder's
	// name and its entries edited too.
	fixture.Write("src/a/x.txt", kLineTwoEdit);
	fixture.Remove("src/b/z.txt");
	fixture.Remove("src/b/deep/w.txt");
	fixture.Write("src/c/new.txt", "new\n");
	fixture.Remove("top.txt");
	fixture.Write("top.txt/inner.txt", kBase);
	fixture.Rename("docs/r.md", "docs/s.md");
	fixture.Write("src/a-b.txt", kLineEightEdit);
	const std::string newer = fixture.Commit("changes");

	git_repository* handle = nullptr;
	REQUIRE(git_repository_open(&handle, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);

	const Core::Result<Core::Repository> opened = Core::Repository::Open(fixture.GetPath());
	REQUIRE(opened.HasValue());
	const Core::Repository& repository = opened.GetValue();

	const ReferenceDiff whole = DiffOfLibgit2(handle, older, newer, {});
	const Core::Result<std::vector<std::string>> walked = repository.PathsChangedBetween(OidOf(older), OidOf(newer));
	REQUIRE(walked.HasValue());
	CHECK(whole.Diffed);
	CHECK(std::set<std::string>{walked.GetValue().begin(), walked.GetValue().end()} == whole.Paths);

	// Every file, folder and near name of either side, alone and in a few lists: the walk and the diff kept to them agree.
	const std::vector<std::string> names{"top.txt", "top.txt/inner.txt", "src", "src/a", "src/a/x.txt", "src/a/y.txt", "src/a-b.txt",
										 "src/a-", "src/b", "src/b/z.txt", "src/b/deep", "src/b/deep/w.txt", "src/c", "src/c/new.txt",
										 "docs", "docs/r.md", "docs/s.md", "kept", "kept/k.txt", "src/a/x", "nowhere", "src/b/",
										 "top.txt/", "src/b/deep/w.txt/below"};
	std::vector<std::vector<std::string>> lists;

	for (const std::string& name : names)
	{
		lists.push_back({name});
	}

	lists.push_back({"kept", "src/a/y.txt", "src/a-"});
	lists.push_back({"kept/k.txt", "src/b/deep"});
	lists.push_back({"docs/s.md", "nowhere"});

	for (const std::vector<std::string>& paths : lists)
	{
		INFO(paths.front(), " and ", paths.size() - 1, " more");
		const ReferenceDiff kept = DiffOfLibgit2(handle, older, newer, paths);
		REQUIRE(kept.Diffed);
		CHECK(TreesDifferAt(repository, older, newer, paths) == !kept.Paths.empty());
		CHECK(TreesDifferAt(repository, newer, older, paths) == !kept.Paths.empty());
	}

	git_repository_free(handle);
}
