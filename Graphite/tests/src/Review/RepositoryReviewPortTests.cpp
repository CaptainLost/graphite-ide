#include <doctest/doctest.h>

#include "Git/RepositoryGitPort.h"
#include "Review/ReviewPort.h"

#include "Review/ReviewRepository.h"

#include <filesystem>
#include <string>

using Graphite::RangeProblem;
using Graphite::ReviewKey;
using Graphite::ReviewRange;

namespace {

	// master and side fork at "fork point"; master then changes a.txt, side adds b.txt and
	// changes a.txt its own way. HEAD is left on master, so side is checked out nowhere.
	struct ForkedHistory
	{
		Core::Oid ForkPoint;
		Core::Oid SideTip;
	};

	ForkedHistory MakeForkedHistory(ReviewRepository& repository)
	{
		ForkedHistory history;

		repository.Write("a.txt", "one\n");
		history.ForkPoint = repository.Commit("fork point");
		repository.Branch("side");

		repository.Write("a.txt", "master\n");
		repository.Commit("master only");

		repository.Checkout("side");
		repository.Write("a.txt", "side\n");
		repository.Write("b.txt", "new on side\n");
		history.SideTip = repository.Commit("side work");

		repository.Checkout("master");
		return history;
	}

	ReviewRange ResolvedRange(Graphite::RepositoryGitPort& port, const ReviewKey& key)
	{
		const Core::Result<ReviewRange> range = port.ResolveRange(key);
		REQUIRE(range.HasValue());
		return range.GetValue();
	}

	const ReviewKey kSideAgainstMaster{.Branch = "refs/heads/side", .Base = "refs/heads/master"};
}

TEST_CASE("A branch's range runs from its fork point to its tip and lists its own commits")
{
	ReviewRepository repository;
	const ForkedHistory history = MakeForkedHistory(repository);
	Graphite::RepositoryGitPort port{repository.GetPath()};

	const ReviewRange range = ResolvedRange(port, kSideAgainstMaster);

	CHECK(range.Problem == RangeProblem::None);
	CHECK(range.Ends.MergeBase == history.ForkPoint);
	CHECK(range.Ends.Tip == history.SideTip);
	CHECK_FALSE(range.Worktree.has_value());
	REQUIRE(range.Commits.size() == 1);
	CHECK(range.Commits.front().Summary == "side work");
	CHECK(range.CommitsComplete);
}

TEST_CASE("A review of a branch that is gone says so rather than failing")
{
	ReviewRepository repository;
	MakeForkedHistory(repository);
	Graphite::RepositoryGitPort port{repository.GetPath()};

	const ReviewRange range = ResolvedRange(port, ReviewKey{.Branch = "refs/heads/gone", .Base = "refs/heads/master"});

	CHECK(range.Problem == RangeProblem::BranchMissing);
}

TEST_CASE("A review against a base that is gone still says where the branch is checked out")
{
	ReviewRepository repository;
	MakeForkedHistory(repository);
	Graphite::RepositoryGitPort port{repository.GetPath()};

	const ReviewRange range = ResolvedRange(port, ReviewKey{.Branch = "refs/heads/master", .Base = "refs/heads/gone"});

	CHECK(range.Problem == RangeProblem::BaseMissing);
	REQUIRE(range.Worktree.has_value());
	CHECK(std::filesystem::equivalent(*range.Worktree, repository.GetPath()));
}

TEST_CASE("A branch with no common ancestor with its base says so")
{
	ReviewRepository repository;
	repository.Write("a.txt", "one\n");
	repository.Commit("one");
	repository.StartOrphan("orphan");
	repository.Write("b.txt", "unrelated\n");
	repository.Commit("a root of its own");
	Graphite::RepositoryGitPort port{repository.GetPath()};

	const ReviewRange range = ResolvedRange(port, ReviewKey{.Branch = "refs/heads/orphan", .Base = "refs/heads/master"});

	CHECK(range.Problem == RangeProblem::NoCommonAncestor);
}

TEST_CASE("The range diff holds what the branch changed and nothing its base changed since")
{
	ReviewRepository repository;
	MakeForkedHistory(repository);
	Graphite::RepositoryGitPort port{repository.GetPath()};
	const ReviewRange range = ResolvedRange(port, kSideAgainstMaster);

	const Core::Result<Core::Diff> diff = port.QueryRangeDiff(range.Ends);
	REQUIRE(diff.HasValue());

	REQUIRE(diff.GetValue().Files.size() == 2);
	CHECK(diff.GetValue().Files[0].NewPath == std::filesystem::path{"a.txt"});
	CHECK(diff.GetValue().Files[1].NewPath == std::filesystem::path{"b.txt"});
}

TEST_CASE("Range texts read the old side at the merge base and the new side at the tip")
{
	ReviewRepository repository;
	MakeForkedHistory(repository);
	Graphite::RepositoryGitPort port{repository.GetPath()};
	const ReviewRange range = ResolvedRange(port, kSideAgainstMaster);

	const Core::Result<Graphite::FileTexts> texts = port.ReadRangeTexts(range.Ends, "a.txt", "a.txt");
	REQUIRE(texts.HasValue());

	CHECK(texts.GetValue().HasOld);
	CHECK(texts.GetValue().Old == "one\n");
	CHECK(texts.GetValue().HasNew);
	CHECK(texts.GetValue().New == "side\n");
}

TEST_CASE("A worktree's diff and texts are its files on disk against the branch tip")
{
	ReviewRepository repository;
	MakeForkedHistory(repository);
	const std::filesystem::path worktree = repository.AddWorktree("side");
	ReviewRepository::WriteFileAt(worktree / "b.txt", "edited in the worktree\n");
	Graphite::RepositoryGitPort port{repository.GetPath()};

	const ReviewRange range = ResolvedRange(port, kSideAgainstMaster);
	REQUIRE(range.Worktree.has_value());
	CHECK(std::filesystem::equivalent(*range.Worktree, worktree));

	const Core::Result<Core::Diff> diff = port.QueryWorktreeDiff(*range.Worktree);
	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);
	CHECK(diff.GetValue().Files.front().NewPath == std::filesystem::path{"b.txt"});

	const Core::Result<Graphite::FileTexts> texts = port.ReadWorktreeTexts(*range.Worktree, "b.txt", "b.txt");
	REQUIRE(texts.HasValue());
	CHECK(texts.GetValue().Old == "new on side\n");
	CHECK(texts.GetValue().New == "edited in the worktree\n");
}
