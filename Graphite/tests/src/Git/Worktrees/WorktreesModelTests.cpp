#include <doctest/doctest.h>

#include "Git/Worktrees/WorktreesModel.h"

#include "DeferredJobs.h"
#include "GitFixtures.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
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

		Core::Result<WorktreeActivity> QueryActivity(const std::filesystem::path&) override { return WorktreeActivity{}; }

		Core::Result<WorktreeCount> CountUncommitted(const std::filesystem::path& worktree) override
		{
			Counted.push_back(worktree);

			if (FailCounts)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "the index is locked"};
			}

			WorktreeCount count;
			count.Changed = Changes[worktree.generic_string()];
			return count;
		}

		std::vector<Core::WorktreeEntry> Entries;
		std::map<std::string, std::size_t> Changes;
		bool FailCounts = false;
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
	model.DirtyInView(kSide);
	model.OnUpdate(Core::Timestep{0.0f});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Counted.size() == 1);
	CHECK(model.IsDirty(kSide) == true);

	model.DirtyInView(kSide);
	model.OnUpdate(Core::Timestep{1.0f});
	jobs.RunAll();
	CHECK(port.Counted.size() == 1);

	port.Changes[kSide.generic_string()] = 0;
	model.DirtyInView(kSide);
	model.OnUpdate(kPoll);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Counted.size() == 2);
	CHECK(model.IsDirty(kSide) == false);

	model.SetOnScreen(false);
	model.DirtyInView(kSide);
	model.OnUpdate(kPoll);
	jobs.RunAll();
	CHECK(port.Counted.size() == 2);
}

TEST_CASE("Git window: a worktree's changes are counted only while its row is in view")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("side", kSide), WorktreeOn("other", kOther)};
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter());

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	jobs.RunAll();
	CHECK(port.Counted.empty());

	model.DirtyInView(kOther);
	model.OnUpdate(Core::Timestep{0.0f});
	jobs.RunAll();

	REQUIRE(port.Counted.size() == 1);
	CHECK(port.Counted[0] == kOther);
}

TEST_CASE("Git window: a failed count of a worktree's changes shows nothing and keeps why")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("side", kSide)};
	port.FailCounts = true;
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter());

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.DirtyInView(kSide);
	model.OnUpdate(Core::Timestep{0.0f});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK_FALSE(model.IsDirty(kSide).has_value());
	CHECK(model.CountFailureOf(kSide) == std::optional<std::string>{"the index is locked"});

	port.FailCounts = false;
	port.Changes[kSide.generic_string()] = 1;
	model.DirtyInView(kSide);
	model.OnUpdate(kPoll);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(model.IsDirty(kSide) == true);
	CHECK_FALSE(model.CountFailureOf(kSide).has_value());
}

TEST_CASE("Git window: a worktree whose count does not change is counted less and less often")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("side", kSide)};
	port.Changes[kSide.generic_string()] = 2;
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter());

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();

	// One update every poll period: counted at 0 s, then 3 s later, then only 6 s after that.
	for (int poll = 0; poll < 4; ++poll)
	{
		model.DirtyInView(kSide);
		model.OnUpdate(poll == 0 ? Core::Timestep{0.0f} : kPoll);
		jobs.RunAll();
		model.OnUpdate(Core::Timestep{0.0f});
	}

	CHECK(port.Counted.size() == 3);

	// The next count is due 12 s after the last, at 21 s; it finds a change, so the one after comes 3 s later.
	port.Changes[kSide.generic_string()] = 0;

	for (int poll = 0; poll < 5; ++poll)
	{
		model.DirtyInView(kSide);
		model.OnUpdate(kPoll);
		jobs.RunAll();
		model.OnUpdate(Core::Timestep{0.0f});
	}

	CHECK(model.IsDirty(kSide) == false);
	CHECK(port.Counted.size() == 5);
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
	model.DirtyInView(kOpen);
	model.DirtyInView(kSide);
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
	model.DirtyInView(kSide);
	model.OnUpdate(Core::Timestep{0.0f});
	model.DirtyInView(kSide);
	model.OnUpdate(kPoll);
	model.DirtyInView(kSide);
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
	model.DirtyInView(kSide);
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
	model.DirtyInView(kSide);
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
	model.DirtyInView(kSide);
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
