#include <doctest/doctest.h>

#include "Git/RepositoryGitPort.h"
#include "Git/Work/WorkPort.h"

#include "Review/ReviewRepository.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace {

	constexpr std::uint8_t kFlipEveryBit = 0xFF;

	// master has one commit; side forks from it with one commit of its own and has a worktree.
	std::filesystem::path MakeSideWorktree(ReviewRepository& repository, Core::Oid& masterTip, Core::Oid& sideTip)
	{
		repository.Write("a.txt", "one\n");
		masterTip = repository.Commit("fork point");
		repository.Branch("side");

		repository.Checkout("side");
		repository.Write("b.txt", "on side\n");
		sideTip = repository.Commit("side work");

		repository.Checkout("master");
		return repository.AddWorktree("side");
	}

	Graphite::WorkFactsRequest RequestFor(const Core::Oid& masterTip, const Core::Oid& sideTip)
	{
		Graphite::WorkFactsRequest request;
		request.TrunkFullName = "refs/heads/master";
		request.TrunkTip = masterTip;
		request.Branches.push_back({.FullName = "refs/heads/side", .Tip = sideTip});
		request.PatchBudget = 100;
		return request;
	}
}

TEST_CASE("Work facts list the worktrees and count each linked worktree's uncommitted files")
{
	ReviewRepository repository;
	Core::Oid masterTip;
	Core::Oid sideTip;
	const std::filesystem::path worktree = MakeSideWorktree(repository, masterTip, sideTip);
	ReviewRepository::WriteFileAt(worktree / "x.txt", "x\n");
	ReviewRepository::WriteFileAt(worktree / "y.txt", "y\n");
	Graphite::RepositoryGitPort port{repository.GetPath()};

	const Core::Result<Graphite::WorkFacts> facts = port.ReadWorkFacts(RequestFor(masterTip, sideTip));

	REQUIRE(facts.HasValue());
	REQUIRE(facts.GetValue().Worktrees.size() == 2);
	CHECK(facts.GetValue().Worktrees[0].Entry.IsMain);
	CHECK_FALSE(facts.GetValue().Worktrees[0].Uncommitted.has_value());
	CHECK_FALSE(facts.GetValue().Worktrees[1].Entry.IsMain);
	REQUIRE(facts.GetValue().Worktrees[1].Uncommitted.has_value());
	CHECK(*facts.GetValue().Worktrees[1].Uncommitted == 2);
}

TEST_CASE("Work facts match each branch's commits against the trunk")
{
	ReviewRepository repository;
	Core::Oid masterTip;
	Core::Oid sideTip;
	MakeSideWorktree(repository, masterTip, sideTip);
	Graphite::RepositoryGitPort port{repository.GetPath()};
	const Graphite::WorkFactsRequest request = RequestFor(masterTip, sideTip);

	const Core::Result<Graphite::WorkFacts> facts = port.ReadWorkFacts(request);

	REQUIRE(facts.HasValue());
	REQUIRE(facts.GetValue().Branches.size() == 1);
	REQUIRE(facts.GetValue().Branches[0].Match.has_value());
	CHECK(facts.GetValue().Branches[0].Match->OwnChanges == 1);
	CHECK(facts.GetValue().Branches[0].Match->InBase == 0);
	CHECK(facts.GetValue().Answers == request);
}

TEST_CASE("A branch whose tip is missing fails alone and the others still answer")
{
	ReviewRepository repository;
	Core::Oid masterTip;
	Core::Oid sideTip;
	MakeSideWorktree(repository, masterTip, sideTip);
	Graphite::RepositoryGitPort port{repository.GetPath()};

	Graphite::WorkFactsRequest request = RequestFor(masterTip, sideTip);
	Core::Oid missing = sideTip;
	for (std::uint8_t& byte : missing.Bytes)
	{
		byte ^= kFlipEveryBit;
	}
	request.Branches.push_back({.FullName = "refs/heads/gone", .Tip = missing});

	const Core::Result<Graphite::WorkFacts> facts = port.ReadWorkFacts(request);

	REQUIRE(facts.HasValue());
	REQUIRE(facts.GetValue().Branches.size() == 2);
	CHECK(facts.GetValue().Branches[0].Match.has_value());
	CHECK_FALSE(facts.GetValue().Branches[0].Failure.has_value());
	CHECK_FALSE(facts.GetValue().Branches[1].Match.has_value());
	CHECK(facts.GetValue().Branches[1].Failure.has_value());
}

TEST_CASE("Counting a worktree's uncommitted files reads that folder alone")
{
	ReviewRepository repository;
	Core::Oid masterTip;
	Core::Oid sideTip;
	const std::filesystem::path worktree = MakeSideWorktree(repository, masterTip, sideTip);
	ReviewRepository::WriteFileAt(worktree / "p.txt", "p\n");
	ReviewRepository::WriteFileAt(worktree / "q.txt", "q\n");
	ReviewRepository::WriteFileAt(worktree / "r.txt", "r\n");
	repository.Write("main-only.txt", "m\n");
	Graphite::RepositoryGitPort port{repository.GetPath()};

	const Core::Result<std::size_t> count = port.CountUncommitted(worktree);

	REQUIRE(count.HasValue());
	CHECK(count.GetValue() == 3);
}
