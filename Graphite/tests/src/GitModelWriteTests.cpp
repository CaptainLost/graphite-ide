#include <doctest/doctest.h>

#include "Git/GitModel.h"

#include "Core/Fs/FileSystem.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace {

	// The same shape as the fake in GitModelTests.cpp, kept separate so a change made for
	// a write test cannot quietly alter what the read tests assert.
	class WritePort final : public Graphite::GitPort
	{
	public:
		Core::Result<Core::WorkingTreeStatus> QueryStatus() override { return Core::WorkingTreeStatus{}; }
		Core::Result<std::vector<Core::Branch>> QueryBranches() override { return std::vector<Core::Branch>{}; }
		Core::Result<Core::LogPage> QueryLog(const Core::LogRequest&) override { return Core::LogPage{}; }
		Core::Result<Core::Diff> QueryDiff(const Core::DiffRequest&) override { return Core::Diff{}; }

		Core::Result<Graphite::FileTexts> ReadFileTexts(
			const Core::Oid&, const std::filesystem::path&, const std::filesystem::path&) override
		{
			return Graphite::FileTexts{};
		}

		Core::Result<void> Stage(const std::vector<std::filesystem::path>& paths) override
		{
			return Record("stage", paths);
		}

		Core::Result<void> Unstage(const std::vector<std::filesystem::path>& paths) override
		{
			return Record("unstage", paths);
		}

		Core::Result<void> DiscardWorkTree(const std::vector<std::filesystem::path>& paths) override
		{
			return Record("discard", paths);
		}

		Core::Result<void> DeleteUntracked(const std::vector<std::filesystem::path>& paths) override
		{
			return Record("delete", paths);
		}

		Core::Result<Core::Oid> Commit(const Core::CommitRequest& request) override
		{
			Calls.push_back("commit:" + request.Message);

			if (Fail)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			return Committed;
		}

		std::vector<std::string> Calls;
		bool Fail = false;
		Core::Oid Committed{};

	private:
		Core::Result<void> Record(const std::string& kind,
								  const std::vector<std::filesystem::path>& paths)
		{
			std::string line = kind + ":";

			for (std::size_t index = 0; index < paths.size(); ++index)
			{
				line += (index == 0 ? "" : ",") + Core::ToUtf8(paths[index]);
			}

			Calls.push_back(line);
			return Fail ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
						: Core::Result<void>::Ok();
		}
	};

	// Runs the job where it was submitted, so a test needs no threads. The port's call is
	// therefore made inside Request*, while the delivery still waits for OnUpdate — which
	// is exactly what makes "one in flight" observable here.
	Graphite::JobSubmitter Inline()
	{
		return [](std::function<void()> job) { job(); };
	}

	// A model with a root set, because every write is dropped when no folder is open.
	void OpenA(Graphite::GitModel& model)
	{
		model.SetRoot("C:/repo");
	}
}

TEST_CASE("A second write waits for the first to be applied")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestStage({"a.txt"});
	model.RequestStage({"b.txt"});

	// Only the first has reached the port: the second is queued behind a write whose
	// result has not been applied yet.
	CHECK(port.Calls == std::vector<std::string>{"stage:a.txt"});
	CHECK(model.IsWriteInFlight());

	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Calls == std::vector<std::string>{"stage:a.txt", "stage:b.txt"});
}

TEST_CASE("Writes leave in the order they were asked for")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestStage({"a.txt"});
	model.RequestUnstage({"b.txt"});
	model.RequestCommit({.Message = "third"});

	for (int pump = 0; pump < 3; ++pump)
	{
		model.OnUpdate(Core::Timestep{0.0f});
	}

	CHECK(port.Calls == std::vector<std::string>{"stage:a.txt", "unstage:b.txt", "commit:third"});
}

TEST_CASE("A successful write refreshes the model")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestStage({"a.txt"});
	model.OnUpdate(Core::Timestep{0.0f});

	// The mark is debounced, so nothing is re-queried until the quiet window elapses.
	CHECK(model.GetStatus() == nullptr);

	model.OnUpdate(Core::Timestep{1.0f});

	CHECK(model.GetStatus() != nullptr);
}

TEST_CASE("A failed write reports itself and refreshes nothing")
{
	WritePort port;
	port.Fail = true;

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestStage({"a.txt"});
	model.OnUpdate(Core::Timestep{0.0f});
	model.OnUpdate(Core::Timestep{1.0f});

	REQUIRE(model.GetWriteError().has_value());
	CHECK_FALSE(model.GetLastError().has_value());
	CHECK(model.GetStatus() == nullptr);

	model.DismissWriteError();
	CHECK_FALSE(model.GetWriteError().has_value());
}

TEST_CASE("TakeCommitted answers once")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCommit({.Message = "one"});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(model.TakeCommitted().has_value());
	CHECK_FALSE(model.TakeCommitted().has_value());
}

TEST_CASE("TakeDiscarded names every path the discard touched")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestDiscard({.Restore = {"a.txt"}, .Delete = {"new.txt"}});
	model.OnUpdate(Core::Timestep{0.0f});

	const std::vector<std::filesystem::path> touched = model.TakeDiscarded();
	CHECK(touched.size() == 2);
	CHECK(model.TakeDiscarded().empty());
}

TEST_CASE("Closing the folder drops a write that had not started")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestStage({"a.txt"});
	model.RequestStage({"b.txt"});

	model.Clear();
	model.OnUpdate(Core::Timestep{0.0f});

	// The first had already reached the port; the queued second must never follow it.
	CHECK(port.Calls == std::vector<std::string>{"stage:a.txt"});
	CHECK_FALSE(model.GetWriteError().has_value());
}

TEST_CASE("A write for a closed folder neither reports nor refreshes")
{
	WritePort port;
	port.Fail = true;

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestStage({"a.txt"});
	model.Clear();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK_FALSE(model.GetWriteError().has_value());
}

TEST_CASE("A write is dropped when no folder is open")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};

	model.RequestStage({"a.txt"});

	CHECK(port.Calls.empty());
	CHECK_FALSE(model.IsWriteInFlight());
}
