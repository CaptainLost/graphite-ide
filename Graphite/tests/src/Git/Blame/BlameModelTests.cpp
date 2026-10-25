#include <doctest/doctest.h>

#include "Git/Blame/BlameModel.h"

#include "Core/Events/EventBus.h"
#include "Core/Git/Repository.h"
#include "Core/Text/TextBuffer.h"
#include "Workspace/Workspace.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using Graphite::BlameModel;
using Graphite::HeadBlame;
using Graphite::LineAuthorKind;

namespace {

	Core::Oid MakeOid(std::uint8_t byte)
	{
		Core::Oid id;
		id.Bytes.fill(byte);
		return id;
	}

	const Core::Oid kCommitA = MakeOid(0xA1);

	HeadBlame Committed(std::string text, std::size_t lineCount)
	{
		auto blame = std::make_shared<Core::Blame>();
		blame->File = "a.txt";
		blame->LineCount = lineCount;
		blame->Hunks = {{1, lineCount, kCommitA, "a.txt", 1}};
		blame->Commits[kCommitA].Id = kCommitA;

		HeadBlame head;
		head.Tracked = true;
		head.Blame = std::move(blame);
		head.HeadText = std::move(text);
		return head;
	}

	class FakeBlamePort final : public Graphite::BlamePort
	{
	public:
		Core::Result<HeadBlame> BlameAtHead(const std::filesystem::path&) override
		{
			++Calls;
			return Head;
		}

		Core::Result<Core::LineHistory> QueryLineHistory(const Core::LineHistoryRequest&) override
		{
			++HistoryCalls;
			return History;
		}

		HeadBlame Head;
		Core::LineHistory History;
		int Calls = 0;
		int HistoryCalls = 0;
	};

	struct HeldJobs
	{
		std::vector<std::function<void()>> Jobs;

		Graphite::JobSubmitter Submitter()
		{
			return [this](std::function<void()> job) { Jobs.push_back(std::move(job)); };
		}

		void RunAll()
		{
			std::vector<std::function<void()>> jobs = std::move(Jobs);
			Jobs.clear();

			for (std::function<void()>& job : jobs)
			{
				job();
			}
		}
	};

	const std::filesystem::path kPath{"C:/repo/a.txt"};

	const Core::Timestep kFrame{0.016f};

	void Settle(BlameModel& model, HeldJobs& jobs, const Core::TextBuffer& buffer)
	{
		for (int round = 0; round < 2; ++round)
		{
			model.Update(kPath, buffer);
			jobs.RunAll();
			model.OnUpdate(kFrame);
		}
	}
}

TEST_CASE("Blame model lays the head blame over the buffer once both have arrived")
{
	FakeBlamePort port;
	port.Head = Committed("a\nb\n", 2);
	HeldJobs jobs;
	BlameModel model{port, jobs.Submitter()};

	const Core::TextBuffer buffer{"a\nX\n"};

	model.Update(kPath, buffer);
	CHECK(model.Get(kPath) == nullptr);

	Settle(model, jobs, buffer);

	const Core::Ref<const Graphite::LineAuthors> authors = model.Get(kPath);
	REQUIRE(authors != nullptr);
	CHECK(authors->Lines[0].Kind == LineAuthorKind::Committed);
	CHECK(authors->Lines[1].Kind == LineAuthorKind::Uncommitted);
}

TEST_CASE("Blame model re-lays the buffer after an edit goes quiet")
{
	FakeBlamePort port;
	port.Head = Committed("a\nb\n", 2);
	HeldJobs jobs;
	BlameModel model{port, jobs.Submitter()};

	Core::TextBuffer buffer{"a\nb\n"};
	Settle(model, jobs, buffer);
	REQUIRE(model.Get(kPath) != nullptr);
	REQUIRE(model.Get(kPath)->Lines[0].Kind == LineAuthorKind::Committed);

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

	CHECK(model.Get(kPath)->Lines[0].Kind == LineAuthorKind::Uncommitted);
}

TEST_CASE("Blame model drops an answer that a newer request overtook")
{
	FakeBlamePort port;
	port.Head = Committed("a\nb\n", 2);
	HeldJobs jobs;
	BlameModel model{port, jobs.Submitter()};

	const Core::TextBuffer buffer{"a\nX\n"};

	model.Update(kPath, buffer);
	REQUIRE(jobs.Jobs.size() == 1);
	std::function<void()> older = std::move(jobs.Jobs.front());
	jobs.Jobs.clear();

	port.Head = Committed("a\nX\n", 2);
	model.Invalidate();
	model.OnUpdate(Core::Timestep{0.2f});
	REQUIRE(jobs.Jobs.size() == 1);

	jobs.RunAll();
	model.OnUpdate(kFrame);

	port.Head = Committed("a\nb\n", 2);
	older();
	model.OnUpdate(kFrame);

	Settle(model, jobs, buffer);

	REQUIRE(model.Get(kPath) != nullptr);
	CHECK(model.Get(kPath)->Lines[1].Kind == LineAuthorKind::Committed);
}

TEST_CASE("Blame model drops an overlay that a reset overtook")
{
	FakeBlamePort port;
	port.Head = Committed("a\nb\n", 2);
	HeldJobs jobs;
	BlameModel model{port, jobs.Submitter()};

	Core::TextBuffer buffer{"a\nb\n"};
	Settle(model, jobs, buffer);
	REQUIRE(model.Get(kPath) != nullptr);

	buffer.Insert(0, "Z\n");
	model.Update(kPath, buffer);
	model.OnUpdate(Core::Timestep{0.2f});
	model.Update(kPath, buffer);
	REQUIRE(jobs.Jobs.size() == 1);
	std::function<void()> overlay = std::move(jobs.Jobs.front());
	jobs.Jobs.clear();

	port.Head = HeadBlame{};
	model.Invalidate();
	model.OnUpdate(Core::Timestep{0.2f});
	REQUIRE(jobs.Jobs.size() == 1);

	jobs.RunAll();
	model.OnUpdate(kFrame);
	model.Update(kPath, buffer);
	CHECK(jobs.Jobs.empty());

	overlay();
	model.OnUpdate(kFrame);

	CHECK(model.Get(kPath) == nullptr);
}

TEST_CASE("Blame model does not re-read the head blame when a file is saved")
{
	FakeBlamePort port;
	port.Head = Committed("a\nb\n", 2);
	HeldJobs jobs;
	BlameModel model{port, jobs.Submitter()};
	Core::EventBus events;
	model.SubscribeTo(events);

	const Core::TextBuffer buffer{"a\nX\n"};
	Settle(model, jobs, buffer);
	REQUIRE(port.Calls == 1);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});

	model.OnUpdate(Core::Timestep{0.2f});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.2f});
	jobs.RunAll();

	CHECK(port.Calls == 1);
}

TEST_CASE("Blame model gives no authors for an untracked file")
{
	FakeBlamePort port;
	HeldJobs jobs;
	BlameModel model{port, jobs.Submitter()};

	const Core::TextBuffer buffer{"a\n"};
	Settle(model, jobs, buffer);

	CHECK(port.Calls == 1);
	CHECK(jobs.Jobs.empty());
	CHECK(model.Get(kPath) == nullptr);
}

TEST_CASE("Blame model re-reads the head blame when the repository changes")
{
	FakeBlamePort port;
	port.Head = Committed("a\nb\n", 2);
	HeldJobs jobs;
	BlameModel model{port, jobs.Submitter()};
	Core::EventBus events;
	model.SubscribeTo(events);

	const Core::TextBuffer buffer{"a\nX\n"};
	Settle(model, jobs, buffer);
	REQUIRE(port.Calls == 1);

	port.Head = Committed("a\nX\n", 2);
	events.Publish(Graphite::RepositoryChangedEvent{});

	model.OnUpdate(kFrame);
	jobs.RunAll();
	CHECK(port.Calls == 1);

	model.OnUpdate(Core::Timestep{0.2f});
	jobs.RunAll();
	CHECK(port.Calls == 2);

	model.OnUpdate(kFrame);
	Settle(model, jobs, buffer);

	CHECK(model.Get(kPath)->Lines[1].Kind == LineAuthorKind::Committed);
}

TEST_CASE("Blame model asks a line history once for the same request")
{
	FakeBlamePort port;
	port.History.Changes.resize(1);
	HeldJobs jobs;
	BlameModel model{port, jobs.Submitter()};

	Core::LineHistoryRequest request;
	request.From = kCommitA;
	request.File = "a.txt";
	request.Line = 2;

	model.RequestLineHistory(kPath, request);
	model.RequestLineHistory(kPath, request);
	CHECK(jobs.Jobs.size() == 1);
	CHECK(model.GetLineHistory(kPath).InFlight);

	jobs.RunAll();
	model.RequestLineHistory(kPath, request);
	CHECK(jobs.Jobs.empty());

	model.OnUpdate(kFrame);

	const Graphite::LineHistoryState state = model.GetLineHistory(kPath);
	CHECK_FALSE(state.InFlight);
	REQUIRE(state.History != nullptr);
	CHECK(state.History->Changes.size() == 1);
	CHECK(port.HistoryCalls == 1);

	model.ForgetLineHistory(kPath);
	CHECK(model.GetLineHistory(kPath).History == nullptr);
}

TEST_CASE("Blame model forgets closed documents")
{
	FakeBlamePort port;
	port.Head = Committed("a\nb\n", 2);
	HeldJobs jobs;
	BlameModel model{port, jobs.Submitter()};

	const Core::TextBuffer buffer{"a\nX\n"};
	Settle(model, jobs, buffer);
	REQUIRE(model.Get(kPath) != nullptr);

	model.Retain([](const std::filesystem::path&) { return false; });

	CHECK(model.Get(kPath) == nullptr);
}
