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

		Core::Result<std::vector<Core::CommitSummary>> QueryLog(const Core::LogRequest&) override
		{
			++LogCalls;
			return Log;
		}

		Core::WorkingTreeStatus Status;
		std::vector<Core::Branch> Branches;
		std::vector<Core::CommitSummary> Log;

		bool FailStatus = false;

		int StatusCalls = 0;
		int BranchCalls = 0;
		int LogCalls = 0;
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
