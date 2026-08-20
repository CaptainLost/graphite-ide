#include <doctest/doctest.h>

#include "Git/GitModel.h"

#include "Core/Events/EventBus.h"
#include "Workspace/Workspace.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace {

	// Answers whatever the test told it to, and counts how often it was asked. No
	// libgit2, no repository on disk, no threads.
	class FakePort final : public Graphite::GitPort
	{
	public:
		Core::Result<Core::WorkingTreeStatus> QueryStatus() override
		{
			++StatusCalls;

			if (FailStatus)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			return Status;
		}

		Core::Result<std::vector<Core::Branch>> QueryBranches() override
		{
			++BranchCalls;
			return Branches;
		}

		Core::Result<Core::LogPage> QueryLog(const Core::LogRequest&) override
		{
			++LogCalls;
			return Log;
		}

		Core::Result<Core::Diff> QueryDiff(const Core::DiffRequest&) override
		{
			++DiffCalls;

			if (FailDiff)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			return Diff;
		}

		Core::WorkingTreeStatus Status;
		std::vector<Core::Branch> Branches;
		Core::LogPage Log;
		Core::Diff Diff;

		bool FailStatus = false;
		bool FailDiff = false;

		int StatusCalls = 0;
		int BranchCalls = 0;
		int LogCalls = 0;
		int DiffCalls = 0;
	};

	// Runs the job where it was submitted. The model still has to hand the result back
	// through OnUpdate, so the test stays honest about the two-step delivery.
	Graphite::JobSubmitter Inline()
	{
		return [](std::function<void()> job) { job(); };
	}

	Core::FileStatus Entry(const char* path)
	{
		Core::FileStatus status;
		status.Path = path;
		status.WorkTree = Core::StatusFlags::Modified;
		return status;
	}
}

TEST_CASE("Before anything is requested the model has no snapshot")
{
	FakePort port;
	Graphite::GitModel model{port, Inline()};

	CHECK(model.GetStatus() == nullptr);
	CHECK(model.GetBranches() == nullptr);
}

TEST_CASE("A requested status becomes readable after the next update")
{
	FakePort port;
	port.Status.Entries.push_back(Entry("a.txt"));

	Graphite::GitModel model{port, Inline()};

	model.RequestStatus();

	// The query has run, but its result is applied on the update that follows, so the
	// view never reads a half-written snapshot mid-frame.
	CHECK(model.GetStatus() == nullptr);

	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetStatus() != nullptr);
	REQUIRE(model.GetStatus()->Entries.size() == 1);
	CHECK(model.GetStatus()->Entries.front().Path == std::filesystem::path("a.txt"));
}

TEST_CASE("A snapshot the view already holds survives the next refresh")
{
	FakePort port;
	port.Status.Entries.push_back(Entry("a.txt"));

	Graphite::GitModel model{port, Inline()};

	model.RequestStatus();
	model.OnUpdate(Core::Timestep{0.016f});

	const std::shared_ptr<const Core::WorkingTreeStatus> held = model.GetStatus();

	port.Status.Entries.clear();
	model.RequestStatus();
	model.OnUpdate(Core::Timestep{0.016f});

	// The view may be halfway through drawing this list. Replacing its contents under it
	// is what the shared pointer exists to prevent.
	REQUIRE(held != nullptr);
	CHECK(held->Entries.size() == 1);
	CHECK(model.GetStatus()->Entries.empty());
}

TEST_CASE("A superseded result is discarded rather than shown")
{
	FakePort port;
	port.Status.Entries.push_back(Entry("stale.txt"));

	Graphite::GitModel model{port, Inline()};

	model.RequestStatus();

	port.Status.Entries.clear();
	port.Status.Entries.push_back(Entry("fresh.txt"));
	model.RequestStatus();

	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetStatus() != nullptr);
	REQUIRE(model.GetStatus()->Entries.size() == 1);

	// Both jobs ran; the older one must not be the one left on screen.
	CHECK(model.GetStatus()->Entries.front().Path == std::filesystem::path("fresh.txt"));
}

TEST_CASE("A failed query leaves the last good snapshot in place")
{
	FakePort port;
	port.Status.Entries.push_back(Entry("a.txt"));

	Graphite::GitModel model{port, Inline()};

	model.RequestStatus();
	model.OnUpdate(Core::Timestep{0.016f});

	port.FailStatus = true;
	model.RequestStatus();
	model.OnUpdate(Core::Timestep{0.016f});

	// Blanking the panel because one refresh failed would be worse than showing data a
	// second out of date.
	REQUIRE(model.GetStatus() != nullptr);
	CHECK(model.GetStatus()->Entries.size() == 1);
	CHECK(model.GetLastError().has_value());
}

TEST_CASE("A branches success does not hide a status failure")
{
	FakePort port;
	port.FailStatus = true;

	Graphite::GitModel model{port, Inline()};

	model.RequestStatus();
	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetLastError().has_value());

	// A different query succeeding must not clear the error a still-failing one left
	// behind - each query kind owns its own error slot.
	model.RequestBranches();
	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetLastError().has_value());
	CHECK(model.GetLastError()->Message == "no");
}

TEST_CASE("An update with nothing pending asks nothing")
{
	FakePort port;
	Graphite::GitModel model{port, Inline()};

	model.OnUpdate(Core::Timestep{0.016f});
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(port.StatusCalls == 0);
	CHECK(port.BranchCalls == 0);
	CHECK(port.LogCalls == 0);
}

TEST_CASE("A file-change event does not refresh until the debounce has elapsed")
{
	FakePort port;
	Core::EventBus events;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{{}, false});

	model.OnUpdate(Core::Timestep{0.05f});
	CHECK(port.StatusCalls == 0);

	model.OnUpdate(Core::Timestep{0.05f});
	CHECK(port.StatusCalls == 0);

	// A save writes several files in a burst. Refreshing on each one would run the query
	// four or five times for one action.
	model.OnUpdate(Core::Timestep{0.1f});
	CHECK(port.StatusCalls == 1);
}

TEST_CASE("A burst of file-change events causes one refresh")
{
	FakePort port;
	Core::EventBus events;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	for (int index = 0; index < 5; ++index)
	{
		events.Publish(Graphite::WorkspaceFilesChangedEvent{{}, false});
		model.OnUpdate(Core::Timestep{0.02f});
	}

	CHECK(port.StatusCalls == 0);

	model.OnUpdate(Core::Timestep{0.2f});
	CHECK(port.StatusCalls == 1);
	CHECK(port.BranchCalls == 1);
}

TEST_CASE("The timer does not keep firing once it has fired")
{
	FakePort port;
	Core::EventBus events;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{{}, false});
	model.OnUpdate(Core::Timestep{0.2f});
	REQUIRE(port.StatusCalls == 1);

	model.OnUpdate(Core::Timestep{1.0f});
	model.OnUpdate(Core::Timestep{1.0f});

	CHECK(port.StatusCalls == 1);
}

TEST_CASE("Closing the workspace drops the snapshots")
{
	FakePort port;
	Core::EventBus events;
	port.Status.Entries.push_back(Entry("a.txt"));

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestStatus();
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetStatus() != nullptr);

	events.Publish(Graphite::WorkspaceClosedEvent{});
	model.OnUpdate(Core::Timestep{0.016f});

	// Leaving the previous folder's changes on screen after it was closed would be a lie
	// about the folder now open.
	CHECK(model.GetStatus() == nullptr);
	CHECK(model.GetBranches() == nullptr);
}

TEST_CASE("A file-change event outside a workspace refreshes nothing")
{
	FakePort port;
	Core::EventBus events;

	Graphite::GitModel model{port, Inline()};
	model.SubscribeTo(events);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{{}, false});
	model.OnUpdate(Core::Timestep{1.0f});

	CHECK(port.StatusCalls == 0);
}

TEST_CASE("Opening a different folder drops the previous folder's snapshot")
{
	FakePort port;
	Core::EventBus events;
	port.Status.Entries.push_back(Entry("a.txt"));

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestStatus();
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetStatus() != nullptr);

	// Workspace::OpenFolder does not always publish WorkspaceClosedEvent first - dropping a
	// second folder onto the window replaces the workspace directly - so the opened handler
	// itself must not leave the previous folder's data looking current.
	events.Publish(Graphite::WorkspaceOpenedEvent{"C:/other"});
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(model.GetStatus() == nullptr);
	CHECK(model.GetBranches() == nullptr);
}

TEST_CASE("A sustained stream of change events still refreshes once the cap is reached")
{
	FakePort port;
	Core::EventBus events;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	// Each mark arrives inside the debounce window and resets the quiet timer, so without a
	// ceiling measured from the first mark this stream would postpone the refresh forever.
	for (int index = 0; index < 9; ++index)
	{
		events.Publish(Graphite::WorkspaceFilesChangedEvent{{}, false});
		model.OnUpdate(Core::Timestep{0.1f});
	}

	CHECK(port.StatusCalls == 0);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{{}, false});
	model.OnUpdate(Core::Timestep{0.1f});

	CHECK(port.StatusCalls == 1);
}

TEST_CASE("A log is not requested without an open workspace")
{
	FakePort port;

	Graphite::GitModel model{port, Inline()};

	// GitView's log-requested latch is reset by WorkspaceClosedEvent without knowing
	// whether a new folder will open next, so the model - not the view - is what must
	// refuse to fire a query for a folder that is not open. Mirrors MarkDirty's own guard.
	model.RequestLog(Core::LogRequest{});
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(port.LogCalls == 0);
	CHECK(model.GetLog() == nullptr);
}

TEST_CASE("A requested diff becomes readable after the next update")
{
	FakePort port;

	Core::FileDiff file;
	file.NewPath = "a.txt";
	file.Change = Core::FileChange::Modified;
	port.Diff.Files.push_back(file);

	Graphite::GitModel model{port, Inline()};

	Core::DiffRequest request;
	request.Target = Core::DiffTarget::WorkTreeToHead;
	model.RequestWorkingTreeDiff(request);

	CHECK(model.GetWorkingTreeDiff() == nullptr);

	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetWorkingTreeDiff() != nullptr);
	REQUIRE(model.GetWorkingTreeDiff()->Files.size() == 1);
	CHECK(model.GetWorkingTreeDiff()->Files.front().NewPath == std::filesystem::path("a.txt"));
}

TEST_CASE("A superseded diff is discarded rather than shown")
{
	FakePort port;

	Core::FileDiff stale;
	stale.NewPath = "stale.txt";
	port.Diff.Files.push_back(stale);

	Graphite::GitModel model{port, Inline()};
	model.RequestWorkingTreeDiff(Core::DiffRequest{});

	port.Diff.Files.clear();
	Core::FileDiff fresh;
	fresh.NewPath = "fresh.txt";
	port.Diff.Files.push_back(fresh);
	model.RequestWorkingTreeDiff(Core::DiffRequest{});

	model.OnUpdate(Core::Timestep{0.016f});

	// Selecting one commit and then another before the first diff lands must not leave
	// the first commit's diff under the second commit's heading.
	REQUIRE(model.GetWorkingTreeDiff() != nullptr);
	REQUIRE(model.GetWorkingTreeDiff()->Files.size() == 1);
	CHECK(model.GetWorkingTreeDiff()->Files.front().NewPath == std::filesystem::path("fresh.txt"));
}

TEST_CASE("A failed diff keeps the last good one and reports the error")
{
	FakePort port;

	Core::FileDiff file;
	file.NewPath = "a.txt";
	port.Diff.Files.push_back(file);

	Graphite::GitModel model{port, Inline()};
	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.OnUpdate(Core::Timestep{0.016f});

	port.FailDiff = true;
	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetWorkingTreeDiff() != nullptr);
	CHECK(model.GetWorkingTreeDiff()->Files.size() == 1);
	CHECK(model.GetLastError().has_value());
}

TEST_CASE("Closing the workspace drops the diff too")
{
	FakePort port;
	Core::EventBus events;

	Core::FileDiff file;
	file.NewPath = "a.txt";
	port.Diff.Files.push_back(file);

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetWorkingTreeDiff() != nullptr);

	events.Publish(Graphite::WorkspaceClosedEvent{});
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(model.GetWorkingTreeDiff() == nullptr);
}

namespace {

	Core::Oid IdOf(std::uint8_t value)
	{
		Core::Oid id;
		id.Bytes[0] = value;
		return id;
	}

	Core::Diff DiffNaming(const char* path)
	{
		Core::Diff diff;

		Core::FileDiff file;
		file.NewPath = path;
		file.Change = Core::FileChange::Modified;
		diff.Files.push_back(std::move(file));

		return diff;
	}
}

TEST_CASE("A commit diff is not requested without an open workspace")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, Inline()};

	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(port.DiffCalls == 0);
	CHECK(model.GetDiffFor(IdOf(1)) == nullptr);
}

TEST_CASE("A commit's diff is readable after the next update, under that commit's id")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(IdOf(1));
	CHECK(model.GetDiffFor(IdOf(1)) == nullptr);

	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetDiffFor(IdOf(1)) != nullptr);
	CHECK(model.GetDiffFor(IdOf(1))->Files.size() == 1);
}

TEST_CASE("A commit nobody asked about has no diff")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});

	// The whole point of keying by commit: an answer can never be shown under a commit
	// it does not belong to.
	CHECK(model.GetDiffFor(IdOf(2)) == nullptr);
}

TEST_CASE("Two commits' diffs are held at the same time")
{
	FakePort port;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	port.Diff = DiffNaming("first.txt");
	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});

	port.Diff = DiffNaming("second.txt");
	model.RequestDiffFor(IdOf(2));
	model.OnUpdate(Core::Timestep{0.016f});

	// Expanding a second commit must not collapse the first — that is the difference
	// between this cache and the single slot it replaces.
	REQUIRE(model.GetDiffFor(IdOf(1)) != nullptr);
	REQUIRE(model.GetDiffFor(IdOf(2)) != nullptr);
	CHECK(model.GetDiffFor(IdOf(1))->Files.front().NewPath == std::filesystem::path("first.txt"));
	CHECK(model.GetDiffFor(IdOf(2))->Files.front().NewPath == std::filesystem::path("second.txt"));
}

TEST_CASE("Asking again for a commit already held does not ask the port again")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});

	const int after = port.DiffCalls;

	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});

	// A commit's diff cannot change, so re-asking is pure waste — and the list asks for
	// every expanded commit on every frame it draws.
	CHECK(port.DiffCalls == after);
}

TEST_CASE("Asking again while the first answer is still in flight does not ask twice")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(IdOf(1));
	model.RequestDiffFor(IdOf(1));

	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(port.DiffCalls == 1);
}

TEST_CASE("A failed diff for a commit leaves no snapshot and reports the error")
{
	FakePort port;
	port.FailDiff = true;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});

	// Unlike the volatile diff, there is no last-good snapshot to keep here: a commit
	// that failed has nothing older to fall back to.
	CHECK(model.GetDiffFor(IdOf(1)) == nullptr);
	CHECK(model.GetLastError().has_value());
}

TEST_CASE("A failed diff is not asked for again on its own")
{
	FakePort port;
	port.FailDiff = true;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});

	const int after = port.DiffCalls;

	// The list asks about every expanded commit every frame. Without suppressing a
	// failed entry the same way a held or in-flight one is suppressed, a permanent
	// failure would resubmit a worker job every one of those frames.
	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(port.DiffCalls == after);
	CHECK(model.GetDiffFor(IdOf(1)) == nullptr);
}

TEST_CASE("A failed diff can be asked for again once forgotten")
{
	FakePort port;
	port.FailDiff = true;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});

	port.FailDiff = false;
	port.Diff = DiffNaming("a.txt");

	// A failure must not poison the entry forever: collapsing and expanding the row
	// again is the reader's recourse, and ForgetFailedDiffFor is what that gesture does.
	model.ForgetFailedDiffFor(IdOf(1));
	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetDiffFor(IdOf(1)) != nullptr);
}

TEST_CASE("Forgetting a failed diff clears the error it left behind")
{
	FakePort port;
	port.FailDiff = true;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetLastError().has_value());

	// The error line must not outlive the row it described: collapsing the row that
	// failed is what erases the entry, and the error should go with it.
	model.ForgetFailedDiffFor(IdOf(1));

	CHECK(!model.GetLastError().has_value());
}

TEST_CASE("Forgetting a successful diff leaves it cached")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetDiffFor(IdOf(1)) != nullptr);

	const int after = port.DiffCalls;

	// Only a failed entry is what collapsing a row is meant to undo. A successful one
	// staying cached is what makes this an LRU cache of recently-seen commits rather
	// than a holder for only the currently-expanded ones.
	model.ForgetFailedDiffFor(IdOf(1));

	REQUIRE(model.GetDiffFor(IdOf(1)) != nullptr);

	// Re-expanding the same row must not re-query git either.
	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(port.DiffCalls == after);
}

TEST_CASE("The cache holds a bounded number of commits")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	for (std::size_t index = 0; index < Graphite::GitModel::kCachedDiffs + 4; ++index)
	{
		model.RequestDiffFor(IdOf(static_cast<std::uint8_t>(index)));
		model.OnUpdate(Core::Timestep{0.016f});
	}

	// Every request here both inserts and is immediately read by nothing else, so eviction
	// picks in strict request order: the four oldest (0-3) are the ones that fall out of a
	// bound of kCachedDiffs once four more than that have been asked for, and everything
	// from 4 onward - a full window of kCachedDiffs entries - survives. Pinning the exact
	// boundary, not just "something was evicted", is what would have caught EvictIfFull
	// dropping only one entry per request and letting the map settle above the bound.
	CHECK(model.GetDiffFor(IdOf(0)) == nullptr);
	CHECK(model.GetDiffFor(IdOf(1)) == nullptr);
	CHECK(model.GetDiffFor(IdOf(2)) == nullptr);
	CHECK(model.GetDiffFor(IdOf(3)) == nullptr);
	CHECK(model.GetDiffFor(IdOf(4)) != nullptr);
	CHECK(model.GetDiffFor(IdOf(static_cast<std::uint8_t>(Graphite::GitModel::kCachedDiffs + 3)))
		  != nullptr);
}

TEST_CASE("Reading a commit's diff protects it from eviction")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	for (std::size_t index = 0; index < Graphite::GitModel::kCachedDiffs; ++index)
	{
		model.RequestDiffFor(IdOf(static_cast<std::uint8_t>(index)));
		model.OnUpdate(Core::Timestep{0.016f});
	}

	// The cache is now exactly full. Reading the oldest entry - which eviction would
	// otherwise pick first - must count as a use, or GetDiffFor's constness over a mutable
	// cache would be lying about what it does.
	REQUIRE(model.GetDiffFor(IdOf(0)) != nullptr);

	model.RequestDiffFor(IdOf(static_cast<std::uint8_t>(Graphite::GitModel::kCachedDiffs)));
	model.OnUpdate(Core::Timestep{0.016f});

	// The entry just read survived; the next-oldest, never read again after its request,
	// is the one that made room instead.
	CHECK(model.GetDiffFor(IdOf(0)) != nullptr);
	CHECK(model.GetDiffFor(IdOf(1)) == nullptr);
}

TEST_CASE("Closing the workspace drops every cached diff")
{
	FakePort port;
	Core::EventBus events;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetDiffFor(IdOf(1)) != nullptr);

	events.Publish(Graphite::WorkspaceClosedEvent{});
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(model.GetDiffFor(IdOf(1)) == nullptr);
}

TEST_CASE("A stale answer from before a clear does not overwrite a fresh one after it")
{
	FakePort port;
	port.Diff = DiffNaming("before.txt");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	// The inline submitter runs the job - and so the query - synchronously here, but the
	// result only reaches the inbox; it is not applied until the OnUpdate below. So this
	// queues the pre-clear delivery without letting it land yet.
	model.RequestDiffFor(IdOf(1));

	// The ordinary reopen path: WorkspaceOpenedEvent's handler calls Clear() before
	// re-requesting, same as the plain "closed" path does.
	model.Clear();

	port.Diff = DiffNaming("after.txt");

	// A fresh entry for the same commit, queuing a second delivery behind the first.
	model.RequestDiffFor(IdOf(1));

	// Both deliveries apply here, in the order they were queued: the pre-clear answer
	// first, then the post-clear one. A generation counted from zero inside the entry -
	// which starts fresh after Clear() erases it - would let the stale delivery win,
	// because it would out-rank the fresh one's own from-zero count. The model-wide
	// counter this cache uses instead never resets, so the fresh request's generation is
	// unconditionally the larger one.
	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetDiffFor(IdOf(1)) != nullptr);
	CHECK(model.GetDiffFor(IdOf(1))->Files.front().NewPath == std::filesystem::path("after.txt"));
}
