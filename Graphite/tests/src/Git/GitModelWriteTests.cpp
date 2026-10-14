#include <doctest/doctest.h>

#include "Git/GitModel.h"

#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Workspace/Workspace.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

	class WritePort final : public Graphite::GitPort
	{
	public:
		Core::Result<Core::WorkingTreeStatus> QueryStatus() override { return Status; }

		Core::Result<Core::WorkingTreeStatus> QueryUntrackedIn(
			const std::filesystem::path&) override
		{
			return Core::WorkingTreeStatus{};
		}

		Core::Result<std::filesystem::path> GetWorkingDirectory() override
		{
			return WorkingDirectory;
		}
		Core::Result<std::vector<Core::Branch>> QueryBranches() override { return std::vector<Core::Branch>{}; }
		Core::Result<Core::HeadState> QueryHead() override { return Core::HeadState{}; }

		Core::Result<void> Init() override { return Record("init", std::vector<std::filesystem::path>{}); }
		Core::Result<Core::LogPage> QueryLog(const Core::LogRequest&) override { return Core::LogPage{}; }
		Core::Result<Core::Diff> QueryDiff(const Core::DiffRequest& request) override
		{
			++DiffCalls;
			DiffTargets.push_back(request.Target);

			if (FailDiff)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			return Core::Diff{};
		}

		std::vector<Core::DiffTarget> DiffTargets;
		bool FailDiff = false;

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
			if (FailDelete)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "cannot delete"};
			}

			return Record("delete", paths);
		}

		Core::Result<Core::Oid> Commit(const Core::CommitRequest& request) override
		{
			Calls.push_back("commit:" + request.Message);

			if (Fail)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "commit failed"};
			}

			return Committed;
		}

		Core::Result<void> CreateBranch(const std::string& name, const Core::Oid&) override
		{
			Calls.push_back("branch:" + name);

			return Fail ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
						: Core::Result<void>::Ok();
		}

		Core::Result<void> RenameBranch(const std::string& from, const std::string& to) override
		{
			Calls.push_back("rename:" + from + ">" + to);

			return Fail ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
						: Core::Result<void>::Ok();
		}

		Core::Result<void> DeleteBranch(const std::string& name) override
		{
			Calls.push_back("delete-branch:" + name);

			return Fail ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
						: Core::Result<void>::Ok();
		}

		Core::Result<void> StashApply(const Core::Oid& id) override
		{
			Calls.push_back("apply:" + id.ShortHex());

			return Fail ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
						: Core::Result<void>::Ok();
		}

		Core::Result<void> StashPop(const Core::Oid& id) override
		{
			Calls.push_back("pop:" + id.ShortHex());

			return Fail ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
						: Core::Result<void>::Ok();
		}

		Core::Result<void> StashDrop(const Core::Oid& id) override
		{
			Calls.push_back("drop:" + id.ShortHex());

			return Fail ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
						: Core::Result<void>::Ok();
		}

		Core::Result<void> DiscardAll(Core::UntrackedFiles untrackedFiles) override
		{
			Calls.push_back(std::string{"discard-all:"} + (untrackedFiles == Core::UntrackedFiles::Included ? "1" : "0"));

			return Fail ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
						: Core::Result<void>::Ok();
		}

		Core::Result<std::size_t> CountUnmergedCommits(const std::string&) override
		{
			if (FailQuery)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			return Unmerged;
		}

		Core::Result<bool> StashSave(const std::string& message, Core::UntrackedFiles untrackedFiles) override
		{
			Calls.push_back("stash:" + message + "|" + (untrackedFiles == Core::UntrackedFiles::Included ? "1" : "0"));

			if (Fail)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			return StashSaves;
		}

		Core::Result<std::vector<Core::StashEntry>> QueryStashes() override
		{
			return std::vector<Core::StashEntry>{};
		}

		Core::Result<Core::CheckoutPlan> PlanCheckout(const Core::CheckoutTarget& target) override
		{
			Calls.push_back("plan:" + TargetLabel(target));

			if (Fail || FailQuery)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			return Plan;
		}

		Core::CheckoutOutcome Checkout(const Core::CheckoutTarget& target) override
		{
			Calls.push_back("checkout:" + TargetLabel(target));

			if (Fail)
			{
				return Core::CheckoutOutcome{{}, Core::Error{Core::ErrorCode::IoFailure, "no"}};
			}

			return CheckoutResult;
		}

		Core::WorkingTreeStatus Status;
		Core::CheckoutPlan Plan;
		Core::CheckoutOutcome CheckoutResult;
		std::size_t Unmerged = 0;
		bool StashSaves = true;

		bool FailQuery = false;

		std::vector<std::string> Calls;
		bool Fail = false;

		bool FailDelete = false;
		int DiffCalls = 0;

		std::filesystem::path WorkingDirectory = "C:/repo";

		bool Throw = false;

		Core::Oid Committed{};

	private:
		static std::string TargetLabel(const Core::CheckoutTarget& target)
		{
			return target.Kind == Core::CheckoutKind::Commit ? target.Commit.ShortHex() : target.Name;
		}

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

			return Fail ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, kind + " failed"}}
						: Core::Result<void>::Ok();
		}
	};

	Graphite::JobSubmitter Inline()
	{
		return [](std::function<void()> job) { job(); };
	}

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

	CHECK(model.GetStatus() == nullptr);

	model.OnUpdate(Core::Timestep{1.0f});

	CHECK(model.GetStatus() != nullptr);

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

	REQUIRE(model.TakeWriteError().has_value());
	CHECK_FALSE(model.GetLastError().has_value());
	CHECK(model.GetStatus() == nullptr);

	CHECK_FALSE(model.TakeCommitted().has_value());
	CHECK(model.TakeDiscarded().empty());

	CHECK_FALSE(model.TakeWriteError().has_value());
}

TEST_CASE("A failure is still reported after a later write succeeds")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	port.Fail = true;
	model.RequestStage({"a.txt"});
	model.OnUpdate(Core::Timestep{0.0f});

	port.Fail = false;
	model.RequestUnstage({"a.txt"});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(model.TakeWriteError().has_value());
	CHECK_FALSE(model.TakeWriteError().has_value());
}

TEST_CASE("Two failed writes are two errors in order")
{
	WritePort port;
	port.Fail = true;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestStage({"a.txt"});
	model.RequestCommit({.Message = "one"});
	model.OnUpdate(Core::Timestep{0.0f});
	model.OnUpdate(Core::Timestep{0.0f});

	const std::optional<Graphite::GitModel::WriteFailure> first = model.TakeWriteError();
	const std::optional<Graphite::GitModel::WriteFailure> second = model.TakeWriteError();
	REQUIRE(first.has_value());
	REQUIRE(second.has_value());
	CHECK(first->Error.Message == "stage failed");
	CHECK(second->Error.Message == "commit failed");
	CHECK(first->Kind == Graphite::GitModel::WriteKind::Stage);
	CHECK(second->Kind == Graphite::GitModel::WriteKind::Commit);
	CHECK_FALSE(model.TakeWriteError().has_value());
}

TEST_CASE("A failed commit is handed out as a commit's failure")
{
	WritePort port;
	port.Fail = true;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCommit({.Message = "one"});
	model.OnUpdate(Core::Timestep{0.0f});

	const std::optional<Graphite::GitModel::WriteFailure> failure = model.TakeWriteError();
	REQUIRE(failure.has_value());
	CHECK(failure->Kind == Graphite::GitModel::WriteKind::Commit);
	CHECK(failure->Error.Message == "commit failed");
}

TEST_CASE("Closing the folder drops failures nobody took")
{
	WritePort port;
	port.Fail = true;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestStage({"a.txt"});
	model.OnUpdate(Core::Timestep{0.0f});

	model.Clear();
	CHECK_FALSE(model.TakeWriteError().has_value());
}

TEST_CASE("The applied write count grows with each successful write and only then")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	CHECK(model.GetAppliedWriteCount() == 0);

	model.RequestStage({"a.txt"});
	model.OnUpdate(Core::Timestep{0.0f});
	CHECK(model.GetAppliedWriteCount() == 1);

	model.RequestCommit({.Message = "one"});
	model.OnUpdate(Core::Timestep{0.0f});
	CHECK(model.GetAppliedWriteCount() == 2);

	port.Fail = true;
	model.RequestUnstage({"a.txt"});
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(model.TakeWriteError().has_value());
	CHECK(model.GetAppliedWriteCount() == 2);
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

TEST_CASE("A discard that restored files and then failed to delete still announces and refreshes")
{
	WritePort port;
	port.FailDelete = true;

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestDiscard({.Restore = {"a.txt"}, .Delete = {"new.txt"}});
	model.OnUpdate(Core::Timestep{0.0f});

	REQUIRE(model.TakeWriteError().has_value());
	CHECK(model.GetAppliedWriteCount() == 1);

	const std::vector<std::filesystem::path> touched = model.TakeDiscarded();
	REQUIRE(touched.size() == 1);
	CHECK(touched[0] == std::filesystem::path{"a.txt"});

	model.OnUpdate(Core::Timestep{1.0f});
	CHECK(model.GetStatus() != nullptr);
}

TEST_CASE("GetWorkingDirectory reports the repository's working directory, not the folder that was opened")
{
	WritePort port;
	port.WorkingDirectory = "C:/repo/nested";

	Graphite::GitModel model{port, Inline()};

	OpenA(model);
	CHECK(model.GetWorkingDirectory().empty());

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

	CHECK(port.Calls == std::vector<std::string>{"stage:a.txt"});
	CHECK_FALSE(model.TakeWriteError().has_value());
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

	CHECK_FALSE(model.TakeWriteError().has_value());

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

	CHECK_FALSE(model.IsWriteInFlight());
	CHECK(model.TakeWriteError().has_value());

	port.Throw = false;
	model.RequestUnstage({"b.txt"});

	CHECK(port.Calls == std::vector<std::string>{"stage:a.txt", "unstage:b.txt"});
}

TEST_CASE("Requesting the working tree diff repeatedly submits only one query")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.RequestWorkingTreeDiff(Core::DiffRequest{});

	CHECK(port.DiffCalls == 1);

	model.OnUpdate(Core::Timestep{0.016f});
	CHECK(port.DiffCalls == 1);

	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.OnUpdate(Core::Timestep{0.016f});
	model.RequestWorkingTreeDiff(Core::DiffRequest{});

	CHECK(port.DiffCalls == 1);
	CHECK(model.GetWorkingTreeDiff() != nullptr);
}

TEST_CASE("A refresh after the working tree diff landed submits it exactly once more")
{
	WritePort port;
	Core::EventBus events;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);
	model.SubscribeTo(events);

	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(port.DiffCalls == 1);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{.Directories = {}, .Everything = false});
	model.OnUpdate(Core::Timestep{0.2f});

	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	CHECK(port.DiffCalls == 2);

	model.OnUpdate(Core::Timestep{0.016f});
	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	CHECK(port.DiffCalls == 2);
}

TEST_CASE("A refresh that runs while the working tree diff is in flight asks again after it lands")
{
	WritePort port;
	Core::EventBus events;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);
	model.SubscribeTo(events);

	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	REQUIRE(port.DiffCalls == 1);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{.Directories = {}, .Everything = false});
	model.OnUpdate(Core::Timestep{0.2f});

	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	CHECK(port.DiffCalls == 2);
}

TEST_CASE("Switching the working tree diff's target shows no stale answer and asks once")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	const Core::DiffRequest unstaged{.Target = Core::DiffTarget::WorkTreeToIndex, .File = "a.txt"};
	const Core::DiffRequest staged{.Target = Core::DiffTarget::IndexToHead, .File = "a.txt"};

	model.RequestWorkingTreeDiff(unstaged);
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetWorkingTreeDiff() != nullptr);

	model.RequestWorkingTreeDiff(staged);
	CHECK(model.GetWorkingTreeDiff() == nullptr);
	CHECK(model.GetWorkingTreeTexts() == nullptr);

	model.RequestWorkingTreeDiff(staged);
	CHECK(port.DiffCalls == 2);

	model.OnUpdate(Core::Timestep{0.016f});
	model.RequestWorkingTreeDiff(staged);

	CHECK(model.GetWorkingTreeDiff() != nullptr);
	CHECK(port.DiffCalls == 2);
	CHECK(port.DiffTargets == std::vector<Core::DiffTarget>{Core::DiffTarget::WorkTreeToIndex, Core::DiffTarget::IndexToHead});
}

TEST_CASE("A working tree diff asked for while another is in flight is sent when that one lands")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	const Core::DiffRequest unstaged{.Target = Core::DiffTarget::WorkTreeToIndex, .File = "a.txt"};
	const Core::DiffRequest staged{.Target = Core::DiffTarget::IndexToHead, .File = "a.txt"};

	model.RequestWorkingTreeDiff(unstaged);
	model.RequestWorkingTreeDiff(staged);

	CHECK(port.DiffCalls == 1);

	model.OnUpdate(Core::Timestep{0.016f});
	CHECK(port.DiffCalls == 2);
	CHECK(model.GetWorkingTreeDiff() == nullptr);

	model.OnUpdate(Core::Timestep{0.016f});
	CHECK(model.GetWorkingTreeDiff() != nullptr);
	CHECK(port.DiffTargets == std::vector<Core::DiffTarget>{Core::DiffTarget::WorkTreeToIndex, Core::DiffTarget::IndexToHead});
}

TEST_CASE("The working tree diff is loading until the wanted request has been answered")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	CHECK_FALSE(model.IsWorkingTreeDiffLoading());

	const Core::DiffRequest unstaged{.Target = Core::DiffTarget::WorkTreeToIndex, .File = "a.txt"};
	const Core::DiffRequest staged{.Target = Core::DiffTarget::IndexToHead, .File = "a.txt"};

	model.RequestWorkingTreeDiff(unstaged);
	CHECK(model.IsWorkingTreeDiffLoading());

	model.OnUpdate(Core::Timestep{0.016f});
	CHECK_FALSE(model.IsWorkingTreeDiffLoading());

	model.RequestWorkingTreeDiff(staged);
	CHECK(model.IsWorkingTreeDiffLoading());

	model.OnUpdate(Core::Timestep{0.016f});
	CHECK_FALSE(model.IsWorkingTreeDiffLoading());

	port.FailDiff = true;
	model.RequestWorkingTreeDiff(unstaged);
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK_FALSE(model.IsWorkingTreeDiffLoading());
	CHECK(model.GetWorkingTreeDiff() == nullptr);
}

TEST_CASE("A failed working tree diff is not asked again every frame")
{
	WritePort port;
	port.FailDiff = true;

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetLastError().has_value());

	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.OnUpdate(Core::Timestep{0.016f});
	model.RequestWorkingTreeDiff(Core::DiffRequest{});

	CHECK(port.DiffCalls == 1);
}

TEST_CASE("An init goes through the write queue like any other write")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestStage({"a.txt"});
	model.RequestInit();

	CHECK(port.Calls == std::vector<std::string>{"stage:a.txt"});

	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Calls == std::vector<std::string>{"stage:a.txt", "init:"});
}

TEST_CASE("A successful init counts as an applied write and refreshes the model")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestInit();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(model.GetAppliedWriteCount() == 1);
	CHECK_FALSE(model.TakeWriteError().has_value());

	model.OnUpdate(Core::Timestep{1.0f});

	CHECK(model.GetStatus() != nullptr);
	CHECK(model.GetHead() != nullptr);
}

TEST_CASE("A failed init is reported as the init's error and not queued as a write error")
{
	WritePort port;
	port.Fail = true;

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestInit();
	model.OnUpdate(Core::Timestep{0.0f});

	REQUIRE(model.GetInitError().has_value());
	CHECK_FALSE(model.TakeWriteError().has_value());
	CHECK(model.GetAppliedWriteCount() == 0);

	model.DismissInitError();
	CHECK_FALSE(model.GetInitError().has_value());
}

TEST_CASE("A failed write other than init is queued as a write error and never shown as the init's")
{
	WritePort port;
	port.Fail = true;

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestStage({"a.txt"});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK_FALSE(model.GetInitError().has_value());
	CHECK(model.TakeWriteError().has_value());
}

TEST_CASE("A successful init clears the error a failed one left")
{
	WritePort port;
	port.Fail = true;

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestInit();
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(model.GetInitError().has_value());

	port.Fail = false;
	model.RequestInit();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK_FALSE(model.GetInitError().has_value());
	CHECK_FALSE(model.TakeWriteError().has_value());
}

TEST_CASE("A second init queued behind the first is passed on, not dropped")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestInit();
	model.RequestInit();
	model.OnUpdate(Core::Timestep{0.0f});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Calls == std::vector<std::string>{"init:", "init:"});
	CHECK_FALSE(model.TakeWriteError().has_value());
}

TEST_CASE("No init is sent with no folder open")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};

	model.RequestInit();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Calls.empty());
}

TEST_CASE("A successful init keeps the model settling until the next status lands")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	CHECK_FALSE(model.IsAwaitingStatusAfterInit());

	model.RequestInit();
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK_FALSE(model.IsWriteInFlight());
	CHECK(model.IsAwaitingStatusAfterInit());

	model.OnUpdate(Core::Timestep{1.0f});

	REQUIRE(model.GetStatus() != nullptr);
	CHECK_FALSE(model.IsAwaitingStatusAfterInit());
}

TEST_CASE("A failed init leaves nothing to settle")
{
	WritePort port;
	port.Fail = true;

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestInit();
	model.OnUpdate(Core::Timestep{0.0f});

	REQUIRE(model.GetInitError().has_value());
	CHECK_FALSE(model.IsAwaitingStatusAfterInit());
}

TEST_CASE("Closing the folder forgets an init that was settling")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestInit();
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(model.IsAwaitingStatusAfterInit());

	model.Clear();

	CHECK_FALSE(model.IsAwaitingStatusAfterInit());
}

TEST_CASE("A checkout with nothing to prepare is one checkout and reports what it changed")
{
	WritePort port;
	port.CheckoutResult.Changed = {"a.txt"};
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCheckout({.Target = {.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"}});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Calls == std::vector<std::string>{"checkout:feature"});
	CHECK(model.TakeCheckedOut() == std::vector<std::filesystem::path>{"a.txt"});
	CHECK(model.TakeCheckedOut().empty());
}

TEST_CASE("A checkout that stashes first stashes, then checks out, and reports the stashed files too")
{
	WritePort port;
	Core::FileStatus edited;
	edited.Path = "b.txt";
	edited.WorkTree = Core::StatusFlags::Modified;
	port.Status.Entries = {edited};
	port.CheckoutResult.Changed = {"a.txt"};

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCheckout({.Target = {.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"},
						   .Preparation = Graphite::CheckoutPreparation::Stash,
						   .StashMessage = "before feature"});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Calls == std::vector<std::string>{"stash:before feature|0", "checkout:feature"});

	const std::vector<std::filesystem::path> checkedOutPaths = model.TakeCheckedOut();
	CHECK(std::find(checkedOutPaths.begin(), checkedOutPaths.end(), std::filesystem::path{"a.txt"}) !=
		  checkedOutPaths.end());
	CHECK(std::find(checkedOutPaths.begin(), checkedOutPaths.end(), std::filesystem::path{"b.txt"}) !=
		  checkedOutPaths.end());
}

TEST_CASE("A checkout that discards first discards, then checks out")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCheckout({.Target = {.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"},
						   .Preparation = Graphite::CheckoutPreparation::Discard,
						   .IncludeUntracked = true});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Calls == std::vector<std::string>{"discard-all:1", "checkout:feature"});
}

TEST_CASE("A preparation leaves out untracked files it was not asked to touch")
{
	WritePort port;
	Core::FileStatus edited;
	edited.Path = "b.txt";
	edited.WorkTree = Core::StatusFlags::Modified;
	Core::FileStatus untracked;
	untracked.Path = "new.txt";
	untracked.IsUntracked = true;
	port.Status.Entries = {edited, untracked};

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCheckout({.Target = {.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"},
						   .Preparation = Graphite::CheckoutPreparation::Discard});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(model.TakeCheckedOut() == std::vector<std::filesystem::path>{"b.txt"});
}

TEST_CASE("A stash with nothing to stash does not stop the checkout")
{
	WritePort port;
	port.StashSaves = false;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCheckout({.Target = {.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"},
						   .Preparation = Graphite::CheckoutPreparation::Stash});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Calls == std::vector<std::string>{"stash:|0", "checkout:feature"});
	CHECK_FALSE(model.TakeWriteError().has_value());
}

TEST_CASE("A failed preparation stops before the checkout")
{
	WritePort port;
	port.Fail = true;

	Core::FileStatus edited;
	edited.Path = "b.txt";
	edited.WorkTree = Core::StatusFlags::Modified;
	port.Status.Entries = {edited};

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCheckout({.Target = {.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"},
						   .Preparation = Graphite::CheckoutPreparation::Discard});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Calls == std::vector<std::string>{"discard-all:0"});
	CHECK(model.TakeWriteError().has_value());
	CHECK(model.TakeCheckedOut().empty());

	model.OnUpdate(Core::Timestep{1.0f});
	CHECK(model.GetStatus() != nullptr);
}

TEST_CASE("A checkout that fails after a successful discard still reports the discarded files")
{
	WritePort port;
	Core::FileStatus edited;
	edited.Path = "b.txt";
	edited.WorkTree = Core::StatusFlags::Modified;
	port.Status.Entries = {edited};
	port.CheckoutResult.Failure = Core::Error{Core::ErrorCode::IoFailure, "no"};

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCheckout({.Target = {.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"},
						   .Preparation = Graphite::CheckoutPreparation::Discard});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Calls == std::vector<std::string>{"discard-all:0", "checkout:feature"});
	CHECK(model.TakeWriteError().has_value());

	CHECK(model.TakeCheckedOut() == std::vector<std::filesystem::path>{"b.txt"});
}

TEST_CASE("A checkout that fails partway reports every path it may have changed")
{
	WritePort port;
	port.CheckoutResult.Changed = {"a.txt", "c.txt"};
	port.CheckoutResult.Failure = Core::Error{Core::ErrorCode::IoFailure, "no"};

	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCheckout({.Target = {.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"}});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(model.TakeWriteError().has_value());
	CHECK(model.TakeCheckedOut() == std::vector<std::filesystem::path>{"a.txt", "c.txt"});
}

TEST_CASE("A plan waits its turn behind writes and answers once")
{
	WritePort port;
	port.Plan.Overwritten = {"a.txt"};
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestStage({"x.txt"});
	model.RequestCheckoutPlan({.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"});
	model.OnUpdate(Core::Timestep{0.0f});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Calls == std::vector<std::string>{"stage:x.txt", "plan:feature"});

	const std::optional<Graphite::CheckoutPlanAnswer> planAnswer = model.TakeCheckoutPlan();
	REQUIRE(planAnswer.has_value());
	CHECK(planAnswer->Target.Name == "feature");
	REQUIRE(planAnswer->Plan.has_value());
	CHECK(planAnswer->Plan->Overwritten == std::vector<std::filesystem::path>{"a.txt"});
	CHECK_FALSE(planAnswer->Failure.has_value());
	CHECK_FALSE(model.TakeCheckoutPlan().has_value());

	CHECK(model.GetAppliedWriteCount() == 1);
}

TEST_CASE("A plan landing after a failed write leaves that write's error in place")
{
	WritePort port;
	port.FailDelete = true;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestDiscard({.Delete = {"y.txt"}});
	model.RequestCheckoutPlan({.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"});
	model.OnUpdate(Core::Timestep{0.0f});
	model.OnUpdate(Core::Timestep{0.0f});

	REQUIRE(model.TakeCheckoutPlan().has_value());
	CHECK(model.TakeWriteError().has_value());
}

TEST_CASE("A failed plan answers through its own one-shot, not the write error")
{
	WritePort port;
	port.FailQuery = true;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCheckoutPlan({.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"});
	model.OnUpdate(Core::Timestep{0.0f});

	const std::optional<Graphite::CheckoutPlanAnswer> planAnswer = model.TakeCheckoutPlan();
	REQUIRE(planAnswer.has_value());
	CHECK(planAnswer->Target.Name == "feature");
	CHECK(planAnswer->Failure.has_value());
	CHECK_FALSE(planAnswer->Plan.has_value());
	CHECK_FALSE(model.TakeWriteError().has_value());
}

TEST_CASE("A plan asked for right after a new branch reaches the port after the branch exists")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCreateBranch("topic", Core::Oid{});
	model.RequestCheckoutPlan({.Kind = Core::CheckoutKind::LocalBranch, .Name = "topic"});
	model.OnUpdate(Core::Timestep{0.0f});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Calls == std::vector<std::string>{"branch:topic", "plan:topic"});
	CHECK(model.TakeCheckoutPlan().has_value());
}

TEST_CASE("Creating a branch reports nothing checked out")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCreateBranch("topic", Core::Oid{});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.Calls == std::vector<std::string>{"branch:topic"});
	CHECK(model.TakeCheckedOut().empty());
	CHECK(model.GetAppliedWriteCount() == 1);
}

TEST_CASE("Branch and stash writes reach the port in order")
{
	WritePort port;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	Core::Oid id;
	id.Bytes[0] = 0xab;

	model.RequestRenameBranch("a", "b");
	model.RequestDeleteBranch("c");
	model.RequestStashSave("msg", true);
	model.RequestStashApply(id);
	model.RequestStashPop(id);
	model.RequestStashDrop(id);

	for (int frame = 0; frame < 6; ++frame)
	{
		model.OnUpdate(Core::Timestep{0.0f});
	}

	CHECK(port.Calls == std::vector<std::string>{"rename:a>b", "delete-branch:c", "stash:msg|1",
												 "apply:ab00000", "pop:ab00000", "drop:ab00000"});
	CHECK(model.GetAppliedWriteCount() == 6);
}

TEST_CASE("A delete check answers how many commits only that branch reaches")
{
	WritePort port;
	port.Unmerged = 3;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestDeleteCheck("old");
	model.OnUpdate(Core::Timestep{0.0f});

	const std::optional<Graphite::DeleteCheck> deleteCheck = model.TakeDeleteCheck();
	REQUIRE(deleteCheck.has_value());
	CHECK(deleteCheck->Branch == "old");
	CHECK(deleteCheck->Unmerged == 3);
	CHECK_FALSE(deleteCheck->Failure.has_value());
	CHECK_FALSE(model.TakeDeleteCheck().has_value());

	CHECK(model.GetAppliedWriteCount() == 0);
}

TEST_CASE("A delete check landing after a failed write leaves that write's error in place")
{
	WritePort port;
	port.FailDelete = true;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestDiscard({.Delete = {"y.txt"}});
	model.RequestDeleteCheck("old");
	model.OnUpdate(Core::Timestep{0.0f});
	model.OnUpdate(Core::Timestep{0.0f});

	REQUIRE(model.TakeDeleteCheck().has_value());
	CHECK(model.TakeWriteError().has_value());
}

TEST_CASE("A failed delete check answers through its own one-shot, not the write error")
{
	WritePort port;
	port.FailQuery = true;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestDeleteCheck("old");
	model.OnUpdate(Core::Timestep{0.0f});

	const std::optional<Graphite::DeleteCheck> deleteCheck = model.TakeDeleteCheck();
	REQUIRE(deleteCheck.has_value());
	CHECK(deleteCheck->Branch == "old");
	CHECK(deleteCheck->Failure.has_value());
	CHECK_FALSE(model.TakeWriteError().has_value());
}

TEST_CASE("A failed stash apply or pop still refreshes the model")
{
	Core::Oid id;
	id.Bytes[0] = 0xab;

	for (const bool popsInsteadOfApplying : {false, true})
	{
		CAPTURE(popsInsteadOfApplying);

		WritePort port;
		port.Fail = true;
		Graphite::GitModel model{port, Inline()};
		OpenA(model);

		if (popsInsteadOfApplying)
		{
			model.RequestStashPop(id);
		}
		else
		{
			model.RequestStashApply(id);
		}

		model.OnUpdate(Core::Timestep{0.0f});
		CHECK(model.TakeWriteError().has_value());

		CHECK(model.GetAppliedWriteCount() == 1);

		model.OnUpdate(Core::Timestep{1.0f});
		CHECK(model.GetStatus() != nullptr);
	}
}

TEST_CASE("A checkout's failure names the target, and where the prepared changes went")
{
	struct Case
	{
		Graphite::CheckoutPreparation Preparation;
		bool Stashed;
		std::string Expected;
	};

	const Case cases[] = {
		{Graphite::CheckoutPreparation::None, false, "Could not check out feature: no"},
		{Graphite::CheckoutPreparation::Stash, true,
		 "Could not check out feature: no. Your changes were stashed as stash@{0}."},
		{Graphite::CheckoutPreparation::Stash, false, "Could not check out feature: no"},
		{Graphite::CheckoutPreparation::Discard, false,
		 "Could not check out feature: no. Your changes were discarded."},
	};

	for (const Case& testCase : cases)
	{
		CAPTURE(testCase.Expected);

		WritePort port;
		port.StashSaves = testCase.Stashed;
		port.CheckoutResult.Failure = Core::Error{Core::ErrorCode::InvalidArgument, "no"};
		Graphite::GitModel model{port, Inline()};
		OpenA(model);

		model.RequestCheckout({.Target = {.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"},
							   .Preparation = testCase.Preparation});
		model.OnUpdate(Core::Timestep{0.0f});

		const std::optional<Graphite::GitModel::WriteFailure> failure = model.TakeWriteError();
		REQUIRE(failure.has_value());
		CHECK(failure->Error.Message == testCase.Expected);
	}
}

TEST_CASE("A failed preparation names the checkout it was for")
{
	WritePort port;
	port.Fail = true;
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCheckout({.Target = {.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"},
						   .Preparation = Graphite::CheckoutPreparation::Stash});
	model.OnUpdate(Core::Timestep{0.0f});

	const std::optional<Graphite::GitModel::WriteFailure> failure = model.TakeWriteError();
	REQUIRE(failure.has_value());
	CHECK(failure->Error.Message == "Could not check out feature: no");
}

TEST_CASE("A stash with nothing to stash says so instead of passing for done")
{
	for (const bool includeUntracked : {false, true})
	{
		CAPTURE(includeUntracked);

		WritePort port;
		port.StashSaves = false;
		Graphite::GitModel model{port, Inline()};
		OpenA(model);

		model.RequestStashSave("msg", includeUntracked);
		model.OnUpdate(Core::Timestep{0.0f});

		const std::optional<Graphite::GitModel::WriteFailure> failure = model.TakeWriteError();
		REQUIRE(failure.has_value());
		CHECK(failure->Error.Message.starts_with("Nothing to stash"));

		CHECK((failure->Error.Message.find("untracked") != std::string::npos) == !includeUntracked);
		CHECK(model.GetAppliedWriteCount() == 0);
	}
}

TEST_CASE("Closing the folder forgets a plan, a delete check and checked-out paths not yet taken")
{
	WritePort port;
	port.CheckoutResult.Changed = {"a.txt"};
	Graphite::GitModel model{port, Inline()};
	OpenA(model);

	model.RequestCheckoutPlan({.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"});
	model.OnUpdate(Core::Timestep{0.0f});
	model.RequestDeleteCheck("old");
	model.OnUpdate(Core::Timestep{0.0f});
	model.RequestCheckout({.Target = {.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"}});
	model.OnUpdate(Core::Timestep{0.0f});

	model.Clear();

	CHECK_FALSE(model.TakeCheckoutPlan().has_value());
	CHECK_FALSE(model.TakeDeleteCheck().has_value());
	CHECK(model.TakeCheckedOut().empty());
}
