#include <doctest/doctest.h>

#include "Git/Work/WorkModel.h"

#include "DeferredJobs.h"
#include "Git/Work/FakeWorkPort.h"

#include <cstdint>
#include <filesystem>
#include <vector>

using Graphite::WorkModel;

namespace {

	const Core::Timestep kFrame{0.016f};
	const Core::Timestep kPollInterval{3.1f};

	const std::filesystem::path kWorktree = "C:/wt/side";

	Core::Oid OidOf(std::uint8_t seed)
	{
		Core::Oid id;
		id.Bytes[0] = seed;
		return id;
	}

	// main is the trunk through origin/main, feature is HEAD's, and side is the last branch the facts are asked about.
	std::vector<Core::Branch> BranchesWithSideAt(std::uint8_t sideTip)
	{
		Core::Branch remote;
		remote.Name = "origin/main";
		remote.FullName = "refs/remotes/origin/main";
		remote.Kind = Core::RefKind::Remote;
		remote.IsRemoteDefault = true;
		remote.Tip = OidOf(1);

		Core::Branch trunk;
		trunk.Name = "main";
		trunk.FullName = "refs/heads/main";
		trunk.Tip = OidOf(1);
		trunk.Upstream = "origin/main";

		Core::Branch feature;
		feature.Name = "feature";
		feature.FullName = "refs/heads/feature";
		feature.Tip = OidOf(2);
		feature.IsHead = true;

		Core::Branch side;
		side.Name = "side";
		side.FullName = "refs/heads/side";
		side.Tip = OidOf(sideTip);

		return {remote, trunk, feature, side};
	}

	Graphite::WorkFacts FactsWithWorktree(std::size_t uncommitted)
	{
		Graphite::WorkFacts facts;
		Graphite::WorktreeFacts worktree;
		worktree.Entry.Path = kWorktree;
		worktree.Uncommitted = uncommitted;
		facts.Worktrees.push_back(worktree);
		return facts;
	}

	void Settle(WorkModel& model, DeferredJobs& jobs)
	{
		jobs.RunAll();
		model.OnUpdate(kFrame);
	}
}

TEST_CASE("Facts are asked once for the same branches")
{
	FakeWorkPort port;
	DeferredJobs jobs;
	WorkModel model{port, jobs.Submitter()};
	const std::vector<Core::Branch> branches = BranchesWithSideAt(10);

	model.FollowBranches(branches);
	CHECK(model.IsReading());
	Settle(model, jobs);
	model.FollowBranches(branches);
	Settle(model, jobs);

	CHECK(port.FactsReads == 1);
	CHECK_FALSE(model.IsReading());
	REQUIRE(model.GetFacts() != nullptr);
	CHECK(model.GetFacts()->Answers.PatchBudget == WorkModel::kPatchBudget);
}

TEST_CASE("Branches whose tips moved ask the facts again")
{
	FakeWorkPort port;
	DeferredJobs jobs;
	WorkModel model{port, jobs.Submitter()};

	model.FollowBranches(BranchesWithSideAt(10));
	Settle(model, jobs);
	model.FollowBranches(BranchesWithSideAt(11));
	Settle(model, jobs);

	CHECK(port.FactsReads == 2);
	CHECK(port.LastRequest.Branches.back().Tip == OidOf(11));
}

TEST_CASE("A request made while a read is in flight waits for it and only the newest is asked after")
{
	FakeWorkPort port;
	DeferredJobs jobs;
	WorkModel model{port, jobs.Submitter()};

	model.FollowBranches(BranchesWithSideAt(10));
	model.FollowBranches(BranchesWithSideAt(11));
	model.FollowBranches(BranchesWithSideAt(12));

	CHECK(jobs.Count() == 1);

	Settle(model, jobs);
	Settle(model, jobs);

	CHECK(port.FactsReads == 2);
	CHECK(port.LastRequest.Branches.back().Tip == OidOf(12));
	CHECK_FALSE(model.IsReading());
}

TEST_CASE("A failed read keeps the last facts and reports the error")
{
	FakeWorkPort port;
	port.Facts = FactsWithWorktree(2);
	DeferredJobs jobs;
	WorkModel model{port, jobs.Submitter()};

	model.FollowBranches(BranchesWithSideAt(10));
	Settle(model, jobs);
	const Core::Ref<const Graphite::WorkFacts> held = model.GetFacts();
	REQUIRE(held != nullptr);

	port.FactsFailure = Core::Error{.Message = "could not read"};
	model.FollowBranches(BranchesWithSideAt(11));
	Settle(model, jobs);

	CHECK(model.GetFacts() == held);
	REQUIRE(model.GetError().has_value());
	CHECK(model.GetError()->Message == "could not read");
	CHECK_FALSE(model.IsReading());
}

TEST_CASE("A first read hands out its facts as they come and a later read only once it lands whole")
{
	FakeWorkPort port;
	port.Facts = FactsWithWorktree(2);
	port.Steps = {FactsWithWorktree(0)};
	port.Steps.front().Worktrees.front().Uncommitted.reset();
	DeferredJobs jobs;
	WorkModel model{port, jobs.Submitter()};

	// The fake reads on this thread, so what the model shows between the read's steps is looked at from inside it.
	Core::Ref<const Graphite::WorkFacts> shownMidRead;
	port.AfterStep = [&] {
		model.OnUpdate(kFrame);
		shownMidRead = model.GetFacts();
	};

	model.FollowBranches(BranchesWithSideAt(10));
	Settle(model, jobs);

	CHECK(port.LastHadProgress);
	REQUIRE(shownMidRead != nullptr);
	CHECK(shownMidRead->Partial);
	CHECK_FALSE(shownMidRead->Worktrees.at(0).Uncommitted.has_value());
	REQUIRE(model.GetFacts() != nullptr);
	CHECK_FALSE(model.GetFacts()->Partial);
	const Core::Ref<const Graphite::WorkFacts> whole = model.GetFacts();

	model.Refresh();
	Settle(model, jobs);

	CHECK_FALSE(port.LastHadProgress);
	CHECK(shownMidRead == whole);
}

TEST_CASE("A first read that fails takes what it handed out with it")
{
	FakeWorkPort port;
	port.Steps = {FactsWithWorktree(1)};
	port.FactsFailure = Core::Error{.Message = "could not read"};
	DeferredJobs jobs;
	WorkModel model{port, jobs.Submitter()};

	model.FollowBranches(BranchesWithSideAt(10));
	Settle(model, jobs);

	CHECK(model.GetFacts() == nullptr);
	REQUIRE(model.GetError().has_value());
	CHECK(model.GetError()->Message == "could not read");
}

TEST_CASE("Polled worktrees are counted again every three seconds while on screen")
{
	FakeWorkPort port;
	port.Facts = FactsWithWorktree(2);
	port.CountAnswer = 7;
	DeferredJobs jobs;
	WorkModel model{port, jobs.Submitter()};

	model.FollowBranches(BranchesWithSideAt(10));
	Settle(model, jobs);
	const Core::Ref<const Graphite::WorkFacts> before = model.GetFacts();
	REQUIRE(before != nullptr);
	CHECK(before->Worktrees.at(0).Uncommitted == 2);

	model.SetPolled({kWorktree}, true);
	model.OnUpdate(kPollInterval);
	Settle(model, jobs);

	CHECK(port.CountReads == 1);
	REQUIRE(model.GetFacts() != nullptr);
	CHECK(model.GetFacts() != before);
	CHECK(model.GetFacts()->Worktrees.at(0).Uncommitted == 7);
	CHECK(before->Worktrees.at(0).Uncommitted == 2);
}

TEST_CASE("Nothing is polled off screen")
{
	FakeWorkPort port;
	port.Facts = FactsWithWorktree(2);
	DeferredJobs jobs;
	WorkModel model{port, jobs.Submitter()};

	model.FollowBranches(BranchesWithSideAt(10));
	Settle(model, jobs);

	model.SetPolled({kWorktree}, false);
	model.OnUpdate(kPollInterval);
	Settle(model, jobs);

	CHECK(port.CountReads == 0);
	CHECK(model.GetFacts()->Worktrees.at(0).Uncommitted == 2);
}

TEST_CASE("Refresh asks the facts again for the same branches")
{
	FakeWorkPort port;
	DeferredJobs jobs;
	WorkModel model{port, jobs.Submitter()};
	const std::vector<Core::Branch> branches = BranchesWithSideAt(10);

	model.FollowBranches(branches);
	Settle(model, jobs);
	model.Refresh();
	CHECK(model.IsReading());
	Settle(model, jobs);

	CHECK(port.FactsReads == 2);
	CHECK(port.LastRequest.Branches.back().Tip == OidOf(10));
}

TEST_CASE("Clear drops the facts and ignores answers still on their way")
{
	FakeWorkPort port;
	port.Facts = FactsWithWorktree(2);
	DeferredJobs jobs;
	WorkModel model{port, jobs.Submitter()};

	model.FollowBranches(BranchesWithSideAt(10));
	Settle(model, jobs);
	REQUIRE(model.GetFacts() != nullptr);

	model.Refresh();
	model.Clear();
	CHECK(model.GetFacts() == nullptr);
	CHECK_FALSE(model.IsReading());

	Settle(model, jobs);

	CHECK(model.GetFacts() == nullptr);
	CHECK_FALSE(model.GetError().has_value());

	model.FollowBranches(BranchesWithSideAt(10));
	Settle(model, jobs);

	CHECK(port.FactsReads == 3);
	CHECK(model.GetFacts() != nullptr);
}
