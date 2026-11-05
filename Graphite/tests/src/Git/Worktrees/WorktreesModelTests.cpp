#include <doctest/doctest.h>

#include "Git/Worktrees/WorktreesModel.h"

#include "DeferredJobs.h"
#include "GitFixtures.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace Graphite;

namespace {

	using Snapshot = Core::Ref<const std::vector<Core::Branch>>;

	Snapshot NewSnapshot()
	{
		return Core::CreateRef<const std::vector<Core::Branch>>();
	}

	Core::WorktreeEntry WorktreeOn(std::string branch, std::filesystem::path path)
	{
		Core::WorktreeEntry entry;
		entry.Path = std::move(path);
		entry.Head.Kind = Core::HeadKind::Branch;
		entry.Head.BranchName = std::move(branch);
		return entry;
	}

	class FakeWorktreePort final : public WorktreePort
	{
	public:
		Core::Result<std::vector<Core::WorktreeEntry>> QueryWorktrees() override
		{
			++Listed;
			return Entries;
		}

		Core::Result<std::size_t> CountUncommitted(const std::filesystem::path& worktree) override
		{
			Counted.push_back(worktree);
			return Changes[worktree.generic_string()];
		}

		std::vector<Core::WorktreeEntry> Entries;
		std::map<std::string, std::size_t> Changes;
		int Listed = 0;
		std::vector<std::filesystem::path> Counted;
	};

	const std::filesystem::path kOpen = "D:/repo";
	const std::filesystem::path kSide = "D:/repo-side";
	const std::filesystem::path kOther = "D:/repo-other";
	const Core::Timestep kPoll{WorktreesModel::kPollSeconds};
}

TEST_CASE("Git window: worktrees are listed for each branch snapshot")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("main", kOpen), WorktreeOn("side", kSide)};
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter());

	const Snapshot first = NewSnapshot();
	model.Follow(first, kOpen);
	model.Follow(first, kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Listed == 1);
	CHECK(model.WorktreeOf("refs/heads/side") == kSide);
	CHECK(model.WorktreeOf("refs/heads/main") == kOpen);
	CHECK_FALSE(model.WorktreeOf("refs/heads/none").has_value());

	port.Entries = {WorktreeOn("main", kOpen)};
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Listed == 2);
	CHECK_FALSE(model.WorktreeOf("refs/heads/side").has_value());
}

TEST_CASE("Git window: a worktree whose folder is missing, or without a branch, has no entry")
{
	FakeWorktreePort port;
	Core::WorktreeEntry gone = WorktreeOn("side", kSide);
	gone.FolderMissing = true;
	Core::WorktreeEntry detached = WorktreeOn("", kOther);
	detached.Head.Kind = Core::HeadKind::Detached;
	port.Entries = {gone, detached};
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter());

	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK_FALSE(model.WorktreeOf("refs/heads/side").has_value());
}

TEST_CASE("Git window: worktree changes are counted only while on screen")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("main", kOpen), WorktreeOn("side", kSide)};
	port.Changes[kSide.generic_string()] = 2;
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter());

	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(kPoll);
	jobs.RunAll();

	CHECK(port.Counted.empty());
	CHECK_FALSE(model.IsDirty(kSide).has_value());

	model.SetOnScreen(true);
	model.OnUpdate(Core::Timestep{0.0f});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Counted.size() == 1);
	CHECK(model.IsDirty(kSide) == true);

	model.OnUpdate(Core::Timestep{1.0f});
	jobs.RunAll();
	CHECK(port.Counted.size() == 1);

	port.Changes[kSide.generic_string()] = 0;
	model.OnUpdate(kPoll);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Counted.size() == 2);
	CHECK(model.IsDirty(kSide) == false);

	model.SetOnScreen(false);
	model.OnUpdate(kPoll);
	jobs.RunAll();
	CHECK(port.Counted.size() == 2);
}

TEST_CASE("Git window: the open folder's worktree is not counted")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("main", kOpen), WorktreeOn("side", kSide)};
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter());

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	REQUIRE(port.Counted.size() == 1);
	CHECK(port.Counted[0] == kSide);
	CHECK_FALSE(model.IsDirty(kOpen).has_value());
}

TEST_CASE("Git window: worktrees of a folder cleared meanwhile are dropped")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("side", kSide)};
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter());

	model.Follow(NewSnapshot(), kOpen);
	model.Clear();
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK_FALSE(model.WorktreeOf("refs/heads/side").has_value());
}

TEST_CASE("Git window: a worktree is not counted again while its count is still running")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("side", kSide)};
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter());

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	model.OnUpdate(kPoll);
	model.OnUpdate(kPoll);
	jobs.RunAll();

	CHECK(port.Counted.size() == 1);
}

TEST_CASE("Git window: a count asked for a folder cleared meanwhile is dropped")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("side", kSide)};
	port.Changes[kSide.generic_string()] = 2;
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter());

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(jobs.Count() == 1);

	model.Clear();
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Counted.empty());
	CHECK_FALSE(model.IsDirty(kSide).has_value());
}

TEST_CASE("Git window: a count that lands after the window left the screen is kept")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("side", kSide)};
	port.Changes[kSide.generic_string()] = 1;
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter());

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	model.SetOnScreen(false);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(model.IsDirty(kSide) == true);
	CHECK(port.Counted.size() == 1);
}

TEST_CASE("Git window: a count of a worktree that left the list is not kept")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("side", kSide)};
	port.Changes[kSide.generic_string()] = 1;
	std::vector<std::function<void()>> waiting;
	WorktreesModel model(port, [&waiting](std::function<void()> job) { waiting.push_back(std::move(job)); });

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	waiting.back()();
	waiting.clear();
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(waiting.size() == 1);

	port.Entries.clear();
	model.Follow(NewSnapshot(), kOpen);
	REQUIRE(waiting.size() == 2);

	// The new list lands before the count that was asked of the old one.
	waiting[1]();
	waiting[0]();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK_FALSE(model.IsDirty(kSide).has_value());
}

TEST_CASE("Git window: the open folder's own branch has no other worktree, the main worktree's has one from a linked folder")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("main", kOpen), WorktreeOn("side", kSide)};
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter());

	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK_FALSE(model.OtherWorktreeOf("refs/heads/main").has_value());
	CHECK(model.OtherWorktreeOf("refs/heads/side") == kSide);

	model.Follow(NewSnapshot(), kSide);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(model.OtherWorktreeOf("refs/heads/main") == kOpen);
	CHECK_FALSE(model.OtherWorktreeOf("refs/heads/side").has_value());
}
