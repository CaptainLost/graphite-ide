#include <doctest/doctest.h>

#include "Git/GitModel.h"

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
