#include <doctest/doctest.h>

#include "Git/Worktrees/WorktreesModel.h"

#include "DeferredJobs.h"
#include "GitFixtures.h"
#include "WorktreeFixtures.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace Graphite;
using Graphite::Fixtures::FakeWorktreePort;
using Graphite::Fixtures::WorktreeOn;

namespace {

	using Snapshot = Core::Ref<const std::vector<Core::Branch>>;

	Snapshot NewSnapshot()
	{
		return Core::CreateRef<const std::vector<Core::Branch>>();
	}

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
	WorktreesModel model(port, jobs.Submitter(), [] { return std::int64_t{0}; });

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

TEST_CASE("Git window: a worktree whose folder is missing, or without a branch, is listed but has no branch")
{
	FakeWorktreePort port;
	Core::WorktreeEntry gone = WorktreeOn("side", kSide);
	gone.FolderMissing = true;
	Core::WorktreeEntry detached = WorktreeOn("", kOther);
	detached.Head.Kind = Core::HeadKind::Detached;
	port.Entries = {gone, detached};
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter(), [] { return std::int64_t{0}; });

	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK_FALSE(model.WorktreeOf("refs/heads/side").has_value());
	CHECK_FALSE(model.OtherWorktreeOf("refs/heads/side").has_value());

	REQUIRE(model.Entries() != nullptr);
	REQUIRE(model.Entries()->size() == 2);
	CHECK(model.Entries()->at(0).FolderMissing);
	CHECK((*model.Entries())[1].Path == kOther);
}

TEST_CASE("Git window: worktree changes are counted only while on screen")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("main", kOpen), WorktreeOn("side", kSide)};
	port.Changes[kSide.generic_string()] = 2;
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter(), [] { return std::int64_t{0}; });

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

TEST_CASE("Git window: a worktree's changes are counted again only while its row is in view")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("side", kSide), WorktreeOn("other", kOther)};
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter(), [] { return std::int64_t{0}; });

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	CHECK(port.Counted.size() == 2);

	model.DirtyInView(kOther);
	model.OnUpdate(kPoll);
	jobs.RunAll();

	REQUIRE(port.Counted.size() == 3);
	CHECK(port.Counted[2] == kOther);
}

TEST_CASE("Git window: a failed count of a worktree's changes shows nothing and keeps why")
{
	FakeWorktreePort port;
	port.Entries = {WorktreeOn("side", kSide)};
	port.FailCounts = true;
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter(), [] { return std::int64_t{0}; });

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
	WorktreesModel model(port, jobs.Submitter(), [] { return std::int64_t{0}; });

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
		model.DirtyInView(kSide); // the drain stays in view, or the sweep would count it
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
	WorktreesModel model(port, jobs.Submitter(), [] { return std::int64_t{0}; });

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
	WorktreesModel model(port, jobs.Submitter(), [] { return std::int64_t{0}; });

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
	WorktreesModel model(port, jobs.Submitter(), [] { return std::int64_t{0}; });

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
	WorktreesModel model(port, jobs.Submitter(), [] { return std::int64_t{0}; });

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
	WorktreesModel model(port, jobs.Submitter(), [] { return std::int64_t{0}; });

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
	WorktreesModel model(port, [&waiting](std::function<void()> job) { waiting.push_back(std::move(job)); }, [] { return std::int64_t{0}; });

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
	WorktreesModel model(port, jobs.Submitter(), [] { return std::int64_t{0}; });

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

namespace {

	constexpr std::int64_t kNow = 1'000'000;

	std::chrono::sys_seconds At(std::int64_t secondsBeforeNow)
	{
		return std::chrono::sys_seconds{std::chrono::seconds{kNow - secondsBeforeNow}};
	}

	std::filesystem::path Folder(int index)
	{
		return "D:/repo-" + std::to_string(index);
	}

	void ListFolders(FakeWorktreePort& port, int count)
	{
		for (int index = 0; index < count; ++index)
		{
			port.Entries.push_back(WorktreeOn("branch" + std::to_string(index), Folder(index)));
		}
	}
}

TEST_CASE("Git window: the first read counts every worktree once, in parallel")
{
	FakeWorktreePort port;
	ListFolders(port, 5);
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter(), [] { return kNow; });

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK_FALSE(model.IsFirstCountSettled());
	CHECK(jobs.Count() == 5);

	jobs.RunAll();

	CHECK(port.Counted.size() == 5);
	CHECK(port.ActivityQueried.size() == 5);
	CHECK_FALSE(model.IsFirstCountSettled());

	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(model.IsFirstCountSettled());
	CHECK(port.Counted.size() == 5);
}

TEST_CASE("Git window: the first count stops holding the list after 3 s")
{
	FakeWorktreePort port;
	ListFolders(port, 2);
	std::vector<std::function<void()>> waiting;
	WorktreesModel model(port, [&waiting](std::function<void()> job) { waiting.push_back(std::move(job)); }, [] { return kNow; });

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	waiting.back()();
	waiting.clear();
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(waiting.size() == 2);

	// Only the first count lands; the second never does.
	waiting[0]();
	model.OnUpdate(Core::Timestep{2.0f});
	CHECK_FALSE(model.IsFirstCountSettled());

	model.OnUpdate(Core::Timestep{1.0f});
	CHECK(model.IsFirstCountSettled());
}

TEST_CASE("Git window: the list read again while one first count hangs still stops holding the list after 3 s")
{
	FakeWorktreePort port;
	ListFolders(port, 2);
	std::vector<std::function<void()>> waiting;
	WorktreesModel model(port, [&waiting](std::function<void()> job) { waiting.push_back(std::move(job)); }, [] { return kNow; });

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	waiting.back()();
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(waiting.size() == 3);

	// The first worktree's count lands; the second's never does, while every second a new branch snapshot reads the list
	// again.
	waiting[1]();

	for (int second = 1; second <= 3; ++second)
	{
		model.Follow(NewSnapshot(), kOpen);
		waiting.back()();
		model.OnUpdate(Core::Timestep{1.0f});

		CHECK(model.IsFirstCountSettled() == (second == 3));
	}
}

TEST_CASE("Git window: a worktree added once the first count settled does not hold the list again")
{
	FakeWorktreePort port;
	ListFolders(port, 1);
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter(), [] { return kNow; });

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(model.IsFirstCountSettled());

	port.Entries.push_back(WorktreeOn("added", Folder(1)));
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(jobs.Count() == 1); // the added worktree's first count, not landed yet

	CHECK(model.IsFirstCountSettled());
}

TEST_CASE("Git window: the first count holds the list again for a folder opened afresh")
{
	FakeWorktreePort port;
	ListFolders(port, 1);
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter(), [] { return kNow; });

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(model.IsFirstCountSettled());

	model.Clear();
	CHECK_FALSE(model.IsFirstCountSettled());

	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	CHECK_FALSE(model.IsFirstCountSettled());
}

TEST_CASE("Git window: worktrees out of view are swept one at a time, at most every 10 s")
{
	FakeWorktreePort port;
	ListFolders(port, 6);
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter(), [] { return kNow; });

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(port.Counted.size() == 6);

	port.Counted.clear();

	for (int step = 0; step < 30; ++step)
	{
		model.OnUpdate(Core::Timestep{1.0f});
		jobs.RunAll();
	}

	// The oldest count first: the first counts landed in list order.
	REQUIRE(port.Counted.size() == 3);
	CHECK(port.Counted[0] == Folder(0));
	CHECK(port.Counted[1] == Folder(1));
	CHECK(port.Counted[2] == Folder(2));
}

TEST_CASE("Git window: the worktree looked at is counted once when asked, and not again while that count is on its way")
{
	FakeWorktreePort port;
	ListFolders(port, 2);
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter(), [] { return kNow; });

	model.SetOnScreen(true);
	model.SetLookedAt(Folder(0));
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	port.Counted.clear();
	port.ActivityQueried.clear();
	port.Changes[Folder(0).generic_string()] = 4;

	model.RecountNow(Folder(0));
	model.RecountNow(Folder(0));
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Counted == std::vector<std::filesystem::path>{Folder(0)});
	CHECK(port.ActivityQueried == std::vector<std::filesystem::path>{Folder(0)});
	CHECK(model.CountOf(Folder(0)) == std::size_t{4});

	model.RecountNow(kOpen);
	model.RecountNow("D:/not-a-worktree");
	jobs.RunAll();

	CHECK(port.Counted.size() == 1);
}

TEST_CASE("Git window: the worktree looked at is neither polled nor swept")
{
	FakeWorktreePort port;
	ListFolders(port, 3);
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter(), [] { return kNow; });

	model.SetOnScreen(true);
	model.SetLookedAt(Folder(0));
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	port.Counted.clear();

	for (int step = 0; step < 60; ++step)
	{
		model.DirtyInView(Folder(0));
		model.OnUpdate(Core::Timestep{1.0f});
		jobs.RunAll();
	}

	CHECK(port.Counted.size() >= 2);
	CHECK(std::ranges::none_of(port.Counted, [](const std::filesystem::path& counted) { return counted == Folder(0); }));
}

TEST_CASE("Git window: last activity is the newest of reflog, index, HEAD commit and newest change")
{
	FakeWorktreePort port;
	ListFolders(port, 2);
	port.Activity[Folder(0).generic_string()] = WorktreeActivity{At(500), At(300), At(400)};
	port.Newest[Folder(0).generic_string()] = At(200);
	port.Activity[Folder(1).generic_string()] = WorktreeActivity{At(500), At(61), At(400)};
	DeferredJobs jobs;
	WorktreesModel model(port, jobs.Submitter(), [] { return kNow; });

	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), kOpen);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(model.LastActivity(Folder(0)) == At(200));
	CHECK(model.LastActivity(Folder(1)) == At(61));
	CHECK_FALSE(model.IsLive(Folder(0)));
	CHECK_FALSE(model.IsLive(Folder(1)));
	CHECK_FALSE(model.LastActivity(kOpen).has_value());

	// A change 59 s old makes the first live; 61 s old, not.
	port.Newest[Folder(0).generic_string()] = At(59);
	port.Newest[Folder(1).generic_string()] = At(61);
	model.DirtyInView(Folder(0));
	model.DirtyInView(Folder(1));
	model.OnUpdate(kPoll);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(model.LastActivity(Folder(0)) == At(59));
	CHECK(model.IsLive(Folder(0)));
	CHECK_FALSE(model.IsLive(Folder(1)));
}
