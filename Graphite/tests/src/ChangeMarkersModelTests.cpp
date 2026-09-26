#include <doctest/doctest.h>

#include "Git/ChangeMarkersModel.h"

#include "Core/Events/EventBus.h"
#include "Core/Text/TextBuffer.h"
#include "Workspace/Workspace.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using Graphite::ChangeBases;
using Graphite::ChangeKind;
using Graphite::ChangeMarkersModel;

namespace {

	class FakeBasePort final : public Graphite::ChangeBasePort
	{
	public:
		Core::Result<ChangeBases> ReadBases(const std::filesystem::path&) override
		{
			++Calls;

			if (Fail)
				return Core::Error{Core::ErrorCode::IoFailure, "no"};

			return Bases;
		}

		ChangeBases Bases;
		bool Fail = false;
		int Calls = 0;
	};

	ChangeBases Committed(std::string text)
	{
		ChangeBases bases;
		bases.Tracked = true;
		bases.Head = text;
		bases.Index = std::move(text);
		return bases;
	}

	// Holds jobs until the test runs them, so the order they finish in is the test's to choose.
	struct HeldJobs
	{
		std::vector<std::function<void()>> Jobs;

		Graphite::JobSubmitter Submitter()
		{
			return [this](std::function<void()> job) { Jobs.push_back(std::move(job)); };
		}

		void RunAll()
		{
			auto jobs = std::move(Jobs);
			Jobs.clear();

			for (auto& job : jobs)
				job();
		}
	};

	const std::filesystem::path kPath{"C:/repo/a.txt"};

	const Core::Timestep kFrame{0.016f};

	// One Update, then everything it asked for run and applied, twice over: bases, then markers.
	void Settle(ChangeMarkersModel& model, HeldJobs& jobs, const Core::TextBuffer& buffer)
	{
		for (int round = 0; round < 2; ++round)
		{
			model.Update(kPath, buffer);
			jobs.RunAll();
			model.OnUpdate(kFrame);
		}
	}
}

TEST_CASE("Markers are computed once the bases have arrived")
{
	FakeBasePort port;
	port.Bases = Committed("a\nb\n");
	HeldJobs jobs;
	ChangeMarkersModel model{port, jobs.Submitter()};

	const Core::TextBuffer buffer{"a\nX\n"};

	model.Update(kPath, buffer);
	CHECK(model.Get(kPath) == nullptr);

	Settle(model, jobs, buffer);

	const auto markers = model.Get(kPath);
	REQUIRE(markers != nullptr);
	CHECK(markers->Lines[1].Kind == ChangeKind::Modified);
	CHECK_FALSE(markers->Lines[1].Staged);
}

TEST_CASE("An edit is diffed only once the buffer has gone quiet")
{
	FakeBasePort port;
	port.Bases = Committed("a\nb\n");
	HeldJobs jobs;
	ChangeMarkersModel model{port, jobs.Submitter()};

	Core::TextBuffer buffer{"a\nb\n"};
	Settle(model, jobs, buffer);

	buffer.Insert(0, "Z\n");

	model.Update(kPath, buffer);
	CHECK(jobs.Jobs.empty());

	model.OnUpdate(Core::Timestep{0.05f});
	model.Update(kPath, buffer);
	CHECK(jobs.Jobs.empty());

	model.OnUpdate(Core::Timestep{0.06f});
	model.Update(kPath, buffer);
	REQUIRE(jobs.Jobs.size() == 1);

	jobs.RunAll();
	model.OnUpdate(kFrame);

	CHECK(model.Get(kPath)->Lines[0].Kind == ChangeKind::Added);
}

TEST_CASE("A result overtaken by a newer request is dropped")
{
	FakeBasePort port;
	port.Bases = Committed("a\nb\n");
	HeldJobs jobs;
	ChangeMarkersModel model{port, jobs.Submitter()};

	Core::TextBuffer buffer{"a\nb\n"};

	model.Update(kPath, buffer);
	jobs.RunAll();
	model.OnUpdate(kFrame);

	// First markers job, left waiting.
	model.Update(kPath, buffer);
	REQUIRE(jobs.Jobs.size() == 1);
	auto older = std::move(jobs.Jobs.front());
	jobs.Jobs.clear();

	buffer.Insert(0, "Z\n");
	model.Update(kPath, buffer);
	model.OnUpdate(Core::Timestep{0.2f});
	model.Update(kPath, buffer);
	REQUIRE(jobs.Jobs.size() == 1);

	jobs.RunAll();
	older();
	model.OnUpdate(kFrame);

	CHECK(model.Get(kPath)->Lines[0].Kind == ChangeKind::Added);
}

TEST_CASE("The bases are read again after the repository changes")
{
	FakeBasePort port;
	port.Bases = Committed("a\nb\n");
	HeldJobs jobs;
	ChangeMarkersModel model{port, jobs.Submitter()};
	Core::EventBus events;
	model.SubscribeTo(events);

	const Core::TextBuffer buffer{"a\nX\n"};
	Settle(model, jobs, buffer);
	REQUIRE(port.Calls == 1);

	// Committed elsewhere: HEAD and the index now match the buffer.
	port.Bases = Committed("a\nX\n");
	events.Publish(Graphite::RepositoryChangedEvent{});

	model.OnUpdate(kFrame);
	jobs.RunAll();
	CHECK(port.Calls == 1);

	model.OnUpdate(Core::Timestep{0.2f});
	jobs.RunAll();
	CHECK(port.Calls == 2);

	model.OnUpdate(kFrame);
	Settle(model, jobs, buffer);

	CHECK(model.Get(kPath)->Lines[1].Kind == ChangeKind::None);
}

TEST_CASE("A file git does not track has nothing to draw")
{
	FakeBasePort port;
	HeldJobs jobs;
	ChangeMarkersModel model{port, jobs.Submitter()};

	const Core::TextBuffer buffer{"a\n"};
	Settle(model, jobs, buffer);

	CHECK(model.Get(kPath) == nullptr);
}

TEST_CASE("An untracked file is added throughout")
{
	FakeBasePort port;
	port.Bases.Tracked = true;
	HeldJobs jobs;
	ChangeMarkersModel model{port, jobs.Submitter()};

	const Core::TextBuffer buffer{"a\nb\n"};
	Settle(model, jobs, buffer);

	const auto markers = model.Get(kPath);
	REQUIRE(markers != nullptr);
	CHECK(markers->Lines[0].Kind == ChangeKind::Added);
	CHECK(markers->Lines[1].Kind == ChangeKind::Added);
	CHECK(markers->Lines[2].Kind == ChangeKind::None);
}

TEST_CASE("A failed read leaves nothing to draw")
{
	FakeBasePort port;
	port.Fail = true;
	HeldJobs jobs;
	ChangeMarkersModel model{port, jobs.Submitter()};

	const Core::TextBuffer buffer{"a\n"};
	Settle(model, jobs, buffer);

	CHECK(model.Get(kPath) == nullptr);
}

TEST_CASE("A document closed while its job is in flight is forgotten")
{
	FakeBasePort port;
	port.Bases = Committed("a\n");
	HeldJobs jobs;
	ChangeMarkersModel model{port, jobs.Submitter()};

	const Core::TextBuffer buffer{"a\nb\n"};
	model.Update(kPath, buffer);

	model.Retain([](const std::filesystem::path&) { return false; });

	jobs.RunAll();
	model.OnUpdate(kFrame);
	CHECK(model.Get(kPath) == nullptr);

	// Opening it again starts over with a fresh read.
	Settle(model, jobs, buffer);
	CHECK(port.Calls == 2);
	CHECK(model.Get(kPath) != nullptr);
}

TEST_CASE("Closing the workspace drops every marker")
{
	FakeBasePort port;
	port.Bases = Committed("a\n");
	HeldJobs jobs;
	ChangeMarkersModel model{port, jobs.Submitter()};
	Core::EventBus events;
	model.SubscribeTo(events);

	const Core::TextBuffer buffer{"a\nb\n"};
	Settle(model, jobs, buffer);
	REQUIRE(model.Get(kPath) != nullptr);

	events.Publish(Graphite::WorkspaceClosedEvent{});

	CHECK(model.Get(kPath) == nullptr);
}
