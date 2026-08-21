#include <doctest/doctest.h>

#include "Git/GitModel.h"

#include "Core/Fs/FileSystem.h"

#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

	// The same shape as the fake in GitModelTests.cpp, kept separate so a change made for
	// a write test cannot quietly alter what the read tests assert.
	class WritePort final : public Graphite::GitPort
	{
	public:
		Core::Result<Core::WorkingTreeStatus> QueryStatus() override { return Core::WorkingTreeStatus{}; }

		Core::Result<std::filesystem::path> GetWorkingDirectory() override
		{
			return WorkingDirectory;
		}
		Core::Result<std::vector<Core::Branch>> QueryBranches() override { return std::vector<Core::Branch>{}; }
		Core::Result<Core::LogPage> QueryLog(const Core::LogRequest&) override { return Core::LogPage{}; }
		Core::Result<Core::Diff> QueryDiff(const Core::DiffRequest&) override
		{
			++DiffCalls;
			return Core::Diff{};
		}

		Core::Result<Graphite::FileTexts> ReadFileTexts(
			const Core::Oid&, const std::filesystem::path&, const std::filesystem::path&) override
		{
			return Graphite::FileTexts{};
		}

		Core::Result<Graphite::FileTexts> ReadWorkingTreeTexts(
			Core::DiffTarget, const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) override
		{
			++WorkingTreeTextCalls;

			// Both sides joined, so a test can say which paths were asked about without
			// caring which of them the fake stored first.
			LastTextPaths = Core::ToUtf8(oldPath) + "|" + Core::ToUtf8(newPath);

			return WorkingTreeTexts;
		}

		Graphite::FileTexts WorkingTreeTexts;
		int WorkingTreeTextCalls = 0;
		std::string LastTextPaths;

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
		int DiffCalls = 0;

		// Deliberately distinct from the "C:/repo" OpenA() gives GitModel::SetRoot(), so a
		// test can tell GitModel::GetWorkingDirectory() apart from GetRoot() - see "GetWorkingDirectory
		// reports the repository's working directory, not the folder that was opened" below.
		std::filesystem::path WorkingDirectory = "C:/repo";

		// Pins the CRITICAL fix: JobQueue abandons a job that throws, so RunWrite must
		// convert this into a failure outcome rather than let it escape and strand
		// m_WriteInFlight forever.
		bool Throw = false;

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

			if (Throw)
			{
				throw std::runtime_error("write port exploded");
			}

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

	// A stage is not a commit: it must never leave a commit id behind for TakeCommitted
	// to hand out.
	CHECK_FALSE(model.TakeCommitted().has_value());
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

	// A failure must leave neither a commit id nor discarded paths behind - both are the
	// answer to a write that succeeded, and this one did not.
	CHECK_FALSE(model.TakeCommitted().has_value());
	CHECK(model.TakeDiscarded().empty());

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

TEST_CASE("GetWorkingDirectory reports the repository's working directory, not the folder that was opened")
{
	WritePort port;
	port.WorkingDirectory = "C:/repo/nested";

	Graphite::GitModel model{port, Inline()};

	// OpenA() sets the root GitModel was told to open. GitPort::GetWorkingDirectory can
	// answer differently - Core::Repository::Open searches upward for a .git, so opening a
	// subdirectory of a repository resolves to a working directory above it. A caller that
	// joined a status path onto the opened root instead of this would build a path that
	// does not exist.
	OpenA(model);
	CHECK(model.GetWorkingDirectory().empty());

	// The working directory is fetched alongside the status query and delivered with it -
	// nothing is known until the first status has landed.
	model.RequestStatus();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(model.GetWorkingDirectory() == std::filesystem::path{"C:/repo/nested"});
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

	// Pins the bug the name describes: an implementation that called MarkDirty() before
	// checking the generation would still pass every assertion above, since the mark is
	// debounced and nothing here advances the clock past it - until this line does.
	model.OnUpdate(Core::Timestep{1.0f});
	CHECK(model.GetStatus() == nullptr);
}

TEST_CASE("A write is dropped when no folder is open")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};

	model.RequestStage({"a.txt"});

	CHECK(port.Calls.empty());
	CHECK_FALSE(model.IsWriteInFlight());
}

TEST_CASE("A write that throws frees the queue instead of stranding it")
{
	WritePort port;
	port.Throw = true;

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestStage({"a.txt"});
	model.OnUpdate(Core::Timestep{0.0f});

	// JobQueue abandons a job that throws before it ever reaches Deliver. If RunWrite let
	// the exception escape, ApplyWrite - the only place that clears m_WriteInFlight - would
	// never run, and the queue would be wedged for the life of the application.
	CHECK_FALSE(model.IsWriteInFlight());
	CHECK(model.GetWriteError().has_value());

	port.Throw = false;
	model.RequestUnstage({"b.txt"});

	// The assertion that actually pins the bug: a write requested after the one that threw
	// must still reach the port, which it cannot if m_WriteInFlight was left stuck at true.
	CHECK(port.Calls == std::vector<std::string>{"stage:a.txt", "unstage:b.txt"});
}

TEST_CASE("Requesting the working tree diff repeatedly submits only one query")
{
	// The diff window calls RequestWorkingTreeDiff every frame it is open (DiffWindows.cpp),
	// the same way the log list calls RequestDiffFor for every expanded commit every frame.
	// Without an in-flight guard this would submit a worker job every frame forever, and the
	// backlog would never drain while the tab stayed open - see RequestDiffFor's own guard
	// for the sibling case this mirrors.
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.RequestWorkingTreeDiff(Core::DiffRequest{});

	CHECK(port.DiffCalls == 1);

	model.OnUpdate(Core::Timestep{0.016f});
	CHECK(port.DiffCalls == 1);

	// Once the first answer has landed, a fresh request is free to submit again.
	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	CHECK(port.DiffCalls == 2);
}
