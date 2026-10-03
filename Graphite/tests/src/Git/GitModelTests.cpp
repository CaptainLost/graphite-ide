#include <doctest/doctest.h>

#include "Git/GitModel.h"

#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Workspace/Workspace.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
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

		Core::Result<Core::WorkingTreeStatus> QueryUntrackedIn(
			const std::filesystem::path& directory) override
		{
			++UntrackedCalls;
			LastUntrackedDirectory = directory;

			if (FailUntracked)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			const auto named = UntrackedByDirectory.find(directory);

			if (named != UntrackedByDirectory.end())
			{
				return named->second;
			}

			return Core::WorkingTreeStatus{};
		}

		Core::Result<std::filesystem::path> GetWorkingDirectory() override
		{
			return WorkingDirectory;
		}

		Core::Result<Core::HeadState> QueryHead() override
		{
			++HeadCalls;

			if (FailHead)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			return Head;
		}

		Core::Result<void> Init() override
		{
			return Record("init", {});
		}

		Core::Result<std::vector<Core::Branch>> QueryBranches() override
		{
			++BranchCalls;
			return Branches;
		}

		Core::Result<Core::LogPage> QueryLog(const Core::LogRequest& request) override
		{
			++LogCalls;

			// Kept so a test can ask what was queried, not merely how often — a refresh that
			// re-asks the wrong question counts the same as one that asks the right one.
			LastLogRequest = request;

			return Log;
		}

		Core::Result<Core::Diff> QueryDiff(const Core::DiffRequest& request) override
		{
			++DiffCalls;
			DiffTargets.push_back(request.Target);

			if (FailDiff)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			return Diff;
		}

		Core::Result<Graphite::FileTexts> ReadFileTexts(
			const Core::Oid&, const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) override
		{
			++TextCalls;

			if (FailTexts)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			// Keyed by both paths joined, so the fake never has to decide which of them names
			// the file - that decision is what the tests using this are about.
			const auto named = TextsByPaths.find(PathsKey(oldPath, newPath));

			if (named != TextsByPaths.end())
			{
				return named->second;
			}

			return Texts;
		}

		static std::string PathsKey(const std::filesystem::path& oldPath,
									const std::filesystem::path& newPath)
		{
			return oldPath.string() + ">" + newPath.string();
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
			WriteCalls.push_back("commit:" + request.Message);

			if (FailWrites)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			return CommittedOid;
		}

		Core::Result<void> CreateBranch(const std::string& name, const Core::Oid&) override
		{
			WriteCalls.push_back("branch:" + name);

			return FailWrites ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
							  : Core::Result<void>::Ok();
		}

		Core::Result<void> RenameBranch(const std::string& from, const std::string& to) override
		{
			WriteCalls.push_back("rename:" + from + ">" + to);

			return FailWrites ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
							  : Core::Result<void>::Ok();
		}

		Core::Result<void> DeleteBranch(const std::string& name) override
		{
			WriteCalls.push_back("delete-branch:" + name);

			return FailWrites ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
							  : Core::Result<void>::Ok();
		}

		Core::Result<void> StashApply(const Core::Oid& id) override
		{
			WriteCalls.push_back("apply:" + id.ShortHex());

			return FailWrites ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
							  : Core::Result<void>::Ok();
		}

		Core::Result<void> StashPop(const Core::Oid& id) override
		{
			WriteCalls.push_back("pop:" + id.ShortHex());

			return FailWrites ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
							  : Core::Result<void>::Ok();
		}

		Core::Result<void> StashDrop(const Core::Oid& id) override
		{
			WriteCalls.push_back("drop:" + id.ShortHex());

			return FailWrites ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
							  : Core::Result<void>::Ok();
		}

		Core::Result<void> DiscardAll(bool includeUntracked) override
		{
			WriteCalls.push_back(std::string{"discard-all:"} + (includeUntracked ? "1" : "0"));

			return FailWrites ? Core::Result<void>{Core::Error{Core::ErrorCode::IoFailure, "no"}}
							  : Core::Result<void>::Ok();
		}

		Core::Result<std::size_t> CountUnmergedCommits(const std::string&) override
		{
			return Unmerged;
		}

		Core::Result<bool> StashSave(const std::string& message, bool includeUntracked) override
		{
			WriteCalls.push_back("stash:" + message + "|" + (includeUntracked ? "1" : "0"));

			if (FailWrites)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			return StashSaves;
		}

		Core::Result<std::vector<Core::StashEntry>> QueryStashes() override
		{
			++StashCalls;
			return Stashes;
		}

		Core::Result<Core::CheckoutPlan> PlanCheckout(const Core::CheckoutTarget& target) override
		{
			WriteCalls.push_back("plan:" + TargetLabel(target));

			if (FailWrites)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			return Plan;
		}

		Core::CheckoutOutcome Checkout(const Core::CheckoutTarget& target) override
		{
			WriteCalls.push_back("checkout:" + TargetLabel(target));

			if (FailWrites)
			{
				return Core::CheckoutOutcome{{}, Core::Error{Core::ErrorCode::IoFailure, "no"}};
			}

			return CheckoutResult;
		}

		std::vector<Core::StashEntry> Stashes;
		std::vector<Core::DiffTarget> DiffTargets;
		int StashCalls = 0;
		Core::CheckoutPlan Plan;
		Core::CheckoutOutcome CheckoutResult;
		std::size_t Unmerged = 0;
		bool StashSaves = true;

		// Every write in the order it was asked for, as "kind:path,path". A vector of
		// strings rather than of structures: a test asserting order reads better as a
		// literal list than as five constructed objects.
		std::vector<std::string> WriteCalls;
		bool FailWrites = false;
		Core::Oid CommittedOid{};

		Core::WorkingTreeStatus Status;
		std::filesystem::path WorkingDirectory = "C:/repo";
		std::vector<Core::Branch> Branches;
		Core::HeadState Head;
		bool FailHead = false;
		Core::LogPage Log;
		Core::Diff Diff;
		Graphite::FileTexts Texts;

		// What to answer for one particular pair of blob paths, keyed by PathsKey. Anything
		// not listed gets Texts.
		std::map<std::string, Graphite::FileTexts> TextsByPaths;

		bool FailStatus = false;
		bool FailDiff = false;
		bool FailTexts = false;

		int StatusCalls = 0;
		int BranchCalls = 0;
		int HeadCalls = 0;
		int LogCalls = 0;
		std::optional<Core::LogRequest> LastLogRequest;
		int DiffCalls = 0;
		int TextCalls = 0;

		int UntrackedCalls = 0;
		bool FailUntracked = false;
		std::filesystem::path LastUntrackedDirectory;
		std::map<std::filesystem::path, Core::WorkingTreeStatus> UntrackedByDirectory;

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

			WriteCalls.push_back(line);

			if (FailWrites)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "no"};
			}

			return Core::Result<void>::Ok();
		}
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
	model.SetRoot("C:/repo");

	Core::DiffRequest request;
	request.Target = Core::DiffTarget::WorkTreeToHead;
	model.RequestWorkingTreeDiff(request);

	CHECK(model.GetWorkingTreeDiff() == nullptr);

	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetWorkingTreeDiff() != nullptr);
	REQUIRE(model.GetWorkingTreeDiff()->Files.size() == 1);
	CHECK(model.GetWorkingTreeDiff()->Files.front().NewPath == std::filesystem::path("a.txt"));
}

TEST_CASE("A request made while one is already in flight is not resubmitted")
{
	FakePort port;

	Core::FileDiff stale;
	stale.NewPath = "stale.txt";
	port.Diff.Files.push_back(stale);

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.RequestWorkingTreeDiff(Core::DiffRequest{});

	// The diff window asks again on the very next frame, before OnUpdate has had a chance
	// to apply the first answer. That second ask must not reach the port - see
	// GitModel::RequestWorkingTreeDiff's in-flight guard - so changing what the port would
	// answer here must not change what the model eventually shows.
	port.Diff.Files.clear();
	Core::FileDiff fresh;
	fresh.NewPath = "fresh.txt";
	port.Diff.Files.push_back(fresh);
	model.RequestWorkingTreeDiff(Core::DiffRequest{});

	CHECK(port.DiffCalls == 1);

	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetWorkingTreeDiff() != nullptr);
	REQUIRE(model.GetWorkingTreeDiff()->Files.size() == 1);
	CHECK(model.GetWorkingTreeDiff()->Files.front().NewPath == std::filesystem::path("stale.txt"));
}

TEST_CASE("A failed diff keeps the last good one and reports the error")
{
	FakePort port;

	Core::FileDiff file;
	file.NewPath = "a.txt";
	port.Diff.Files.push_back(file);

	Core::EventBus events;
	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);
	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.OnUpdate(Core::Timestep{0.016f});

	// The same request is only asked again after a refresh has marked its answer stale.
	port.FailDiff = true;
	events.Publish(Graphite::WorkspaceFilesChangedEvent{{}, false});
	model.OnUpdate(Core::Timestep{0.2f});
	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	REQUIRE(port.DiffCalls == 2);
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

TEST_CASE("A commit's diff reports failed only while a failed answer is held")
{
	FakePort port;
	port.FailDiff = true;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	// Never asked about, and asked but not yet answered: neither is a failure, and a diff
	// tab restored from the last launch must not be closed for either.
	CHECK_FALSE(model.IsDiffFailed(IdOf(1)));
	model.RequestDiffFor(IdOf(1));
	CHECK_FALSE(model.IsDiffFailed(IdOf(1)));

	model.OnUpdate(Core::Timestep{0.016f});
	CHECK(model.IsDiffFailed(IdOf(1)));

	model.ForgetFailedDiffFor(IdOf(1));
	CHECK_FALSE(model.IsDiffFailed(IdOf(1)));

	port.FailDiff = false;
	port.Diff = DiffNaming("a.txt");
	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK_FALSE(model.IsDiffFailed(IdOf(1)));
	CHECK(model.GetDiffFor(IdOf(1)) != nullptr);
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
	CHECK(model.GetDiffFor(IdOf(static_cast<std::uint8_t>(Graphite::GitModel::kCachedDiffs + 3))) != nullptr);
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

namespace {

	Graphite::FileTexts TextsNaming(const char* text)
	{
		Graphite::FileTexts texts;
		texts.Old = text;
		texts.New = text;
		texts.HasOld = true;
		texts.HasNew = true;
		return texts;
	}
}

TEST_CASE("A requested file's texts become readable after the next update")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	CHECK(model.GetTextsFor(IdOf(1), "a.txt") == nullptr);

	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetTextsFor(IdOf(1), "a.txt") != nullptr);
	CHECK(model.GetTextsFor(IdOf(1), "a.txt")->Old == "a");
	CHECK(model.GetTextsFor(IdOf(1), "a.txt")->New == "a");
}

TEST_CASE("A commit nobody asked about has no texts")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});

	// The whole point of keying by commit and path: an answer can never be shown under a
	// file it does not belong to.
	CHECK(model.GetTextsFor(IdOf(2), "a.txt") == nullptr);
}

TEST_CASE("Two commits' texts are held at the same time")
{
	FakePort port;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	port.Texts = TextsNaming("first");
	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});

	port.Texts = TextsNaming("second");
	model.RequestTextsFor(IdOf(2), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});

	// Asking about a second commit must not collapse the first.
	REQUIRE(model.GetTextsFor(IdOf(1), "a.txt") != nullptr);
	REQUIRE(model.GetTextsFor(IdOf(2), "a.txt") != nullptr);
	CHECK(model.GetTextsFor(IdOf(1), "a.txt")->Old == "first");
	CHECK(model.GetTextsFor(IdOf(2), "a.txt")->Old == "second");
}

TEST_CASE("Asking again for texts already held does not ask the port again")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});

	const int after = port.TextCalls;

	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});

	// A commit's texts cannot change, so re-asking is pure waste - and the view asks for
	// the same file on every frame it draws.
	CHECK(port.TextCalls == after);
}

TEST_CASE("Asking again for texts while the first answer is still in flight does not ask twice")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");

	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(port.TextCalls == 1);
}

TEST_CASE("A failed texts request for a file leaves no snapshot and reports the error")
{
	FakePort port;
	port.FailTexts = true;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});

	// There is no last-good snapshot to keep: a file whose texts failed has nothing older
	// to fall back to.
	CHECK(model.GetTextsFor(IdOf(1), "a.txt") == nullptr);
	CHECK(model.GetLastError().has_value());
}

TEST_CASE("A failed texts request is not asked for again on its own")
{
	FakePort port;
	port.FailTexts = true;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});

	const int after = port.TextCalls;

	// A permanent failure must not turn the view's every-frame ask into a worker job
	// every frame.
	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(port.TextCalls == after);
	CHECK(model.GetTextsFor(IdOf(1), "a.txt") == nullptr);
}

TEST_CASE("The texts cache holds a bounded number of files")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	for (std::size_t index = 0; index < Graphite::GitModel::kCachedTexts + 4; ++index)
	{
		model.RequestTextsFor(IdOf(static_cast<std::uint8_t>(index)), "a.txt", "a.txt", "a.txt");
		model.OnUpdate(Core::Timestep{0.016f});
	}

	// Same reasoning as the commit-diff cache's boundary test: strict request order means
	// exactly the four oldest fall out of a bound of kCachedTexts once four more than that
	// have been asked for.
	CHECK(model.GetTextsFor(IdOf(0), "a.txt") == nullptr);
	CHECK(model.GetTextsFor(IdOf(1), "a.txt") == nullptr);
	CHECK(model.GetTextsFor(IdOf(2), "a.txt") == nullptr);
	CHECK(model.GetTextsFor(IdOf(3), "a.txt") == nullptr);
	CHECK(model.GetTextsFor(IdOf(4), "a.txt") != nullptr);
	CHECK(model.GetTextsFor(IdOf(static_cast<std::uint8_t>(Graphite::GitModel::kCachedTexts + 3)),
							"a.txt") != nullptr);
}

TEST_CASE("Closing the workspace drops every cached file's texts")
{
	FakePort port;
	Core::EventBus events;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetTextsFor(IdOf(1), "a.txt") != nullptr);

	events.Publish(Graphite::WorkspaceClosedEvent{});
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(model.GetTextsFor(IdOf(1), "a.txt") == nullptr);
}

TEST_CASE("A file's texts are not requested without an open workspace")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, Inline()};

	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(port.TextCalls == 0);
	CHECK(model.GetTextsFor(IdOf(1), "a.txt") == nullptr);
}

TEST_CASE("Reading a file's texts protects it from eviction")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	for (std::size_t index = 0; index < Graphite::GitModel::kCachedTexts; ++index)
	{
		model.RequestTextsFor(IdOf(static_cast<std::uint8_t>(index)), "a.txt", "a.txt", "a.txt");
		model.OnUpdate(Core::Timestep{0.016f});
	}

	// The cache is now exactly full. Reading the oldest entry - which eviction would
	// otherwise pick first - must count as a use, or GetTextsFor's constness over a
	// mutable cache would be lying about what it does.
	REQUIRE(model.GetTextsFor(IdOf(0), "a.txt") != nullptr);

	model.RequestTextsFor(IdOf(static_cast<std::uint8_t>(Graphite::GitModel::kCachedTexts)), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});

	// The entry just read survived; the next-oldest, never read again after its request,
	// is the one that made room instead.
	CHECK(model.GetTextsFor(IdOf(0), "a.txt") != nullptr);
	CHECK(model.GetTextsFor(IdOf(1), "a.txt") == nullptr);
}

TEST_CASE("A stale texts answer from before a clear does not overwrite a fresh one after it")
{
	FakePort port;
	port.Texts = TextsNaming("before");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	// The inline submitter runs the job - and so the query - synchronously here, but the
	// result only reaches the inbox; it is not applied until the OnUpdate below. So this
	// queues the pre-clear delivery without letting it land yet.
	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");

	// The ordinary reopen path: WorkspaceOpenedEvent's handler calls Clear() before
	// re-requesting, same as the plain "closed" path does.
	model.Clear();

	port.Texts = TextsNaming("after");

	// A fresh entry for the same commit and path, queuing a second delivery behind the
	// first.
	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");

	// Both deliveries apply here, in the order they were queued: the pre-clear answer
	// first, then the post-clear one. A generation counted from zero inside the entry -
	// which starts fresh after Clear() erases it - would let the stale delivery win,
	// because it would out-rank the fresh one's own from-zero count. The model-wide
	// counter this cache uses instead never resets, so the fresh request's generation is
	// unconditionally the larger one.
	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetTextsFor(IdOf(1), "a.txt") != nullptr);
	CHECK(model.GetTextsFor(IdOf(1), "a.txt")->Old == "after");
}

TEST_CASE("A failed texts request can be asked for again once forgotten")
{
	FakePort port;
	port.FailTexts = true;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});

	port.FailTexts = false;
	port.Texts = TextsNaming("a");

	// A failure must not poison the entry forever: asking for it again is the reader's
	// recourse, and ForgetFailedTextsFor is what that gesture does.
	model.ForgetFailedTextsFor(IdOf(1), "a.txt");
	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetTextsFor(IdOf(1), "a.txt") != nullptr);
}

TEST_CASE("Forgetting a failed texts request clears the error it left behind")
{
	FakePort port;
	port.FailTexts = true;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetLastError().has_value());

	// The error line must not outlive the entry it described.
	model.ForgetFailedTextsFor(IdOf(1), "a.txt");

	CHECK(!model.GetLastError().has_value());
}

TEST_CASE("Forgetting a successful texts request leaves it cached")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetTextsFor(IdOf(1), "a.txt") != nullptr);

	const int after = port.TextCalls;

	// Only a failed entry is what forgetting is meant to undo. A successful one staying
	// cached is what makes this an LRU cache of recently-seen files rather than a holder
	// for only the currently-viewed one.
	model.ForgetFailedTextsFor(IdOf(1), "a.txt");

	REQUIRE(model.GetTextsFor(IdOf(1), "a.txt") != nullptr);

	// Asking again for the same file must not re-query git either.
	model.RequestTextsFor(IdOf(1), "a.txt", "a.txt", "a.txt");
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(port.TextCalls == after);
}

namespace {

	Core::FileDiff DeletedFile(const char* path)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Deleted;
		file.OldPath = path;
		return file;
	}

	Core::FileDiff AddedFile(const char* path)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Added;
		file.NewPath = path;
		return file;
	}

	Core::FileDiff ModifiedFile(const char* path)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.OldPath = path;
		file.NewPath = path;
		return file;
	}

	Core::FileDiff RenamedFile(const char* oldPath, const char* newPath)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Renamed;
		file.OldPath = oldPath;
		file.NewPath = newPath;
		return file;
	}

	Graphite::FileTexts OldSideOnly(const char* text)
	{
		Graphite::FileTexts texts;
		texts.Old = text;
		texts.HasOld = true;
		return texts;
	}

	Graphite::FileTexts NewSideOnly(const char* text)
	{
		Graphite::FileTexts texts;
		texts.New = text;
		texts.HasNew = true;
		return texts;
	}

	// One frame of what an owner does: ask, let the worker's answer be applied, then ask
	// again so the binding picks the answer up. Two calls because delivery is deliberately
	// two-step - the model only makes a result visible from OnUpdate.
	void Settle(Graphite::FileTextsBinding& binding, Graphite::GitModel& model,
				const Core::Oid& commit, const Core::FileDiff& file)
	{
		binding.Update(model, commit, file);
		model.OnUpdate(Core::Timestep{0.016f});
		binding.Update(model, commit, file);
	}
}

TEST_CASE("Two files deleted in one commit do not share one texts entry")
{
	FakePort port;
	port.TextsByPaths[FakePort::PathsKey("a.cpp", "")] = OldSideOnly("a");
	port.TextsByPaths[FakePort::PathsKey("b.cpp", "")] = OldSideOnly("b");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	Settle(binding, model, IdOf(1), DeletedFile("a.cpp"));

	REQUIRE(binding.Texts != nullptr);
	CHECK(binding.Texts->Old == "a");

	// Neither file has a new path at all - that is what "deleted" means - so a cache keyed
	// by the new path would hand the second file the first one's text under the key {commit,
	// ""}, and the unchanged pointer would leave its tokens alone too.
	Settle(binding, model, IdOf(1), DeletedFile("b.cpp"));

	REQUIRE(binding.Texts != nullptr);
	CHECK(binding.Texts->Old == "b");

	CHECK(model.GetTextsFor(IdOf(1), "a.cpp") != nullptr);
	CHECK(model.GetTextsFor(IdOf(1), "b.cpp") != nullptr);
	CHECK(model.GetTextsFor(IdOf(1), "") == nullptr);
}

TEST_CASE("A deleted file and an added file in one commit do not collide")
{
	FakePort port;
	port.TextsByPaths[FakePort::PathsKey("gone.cpp", "")] = OldSideOnly("gone");
	port.TextsByPaths[FakePort::PathsKey("", "fresh.cpp")] = NewSideOnly("fresh");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	Settle(binding, model, IdOf(1), DeletedFile("gone.cpp"));

	REQUIRE(binding.Texts != nullptr);
	CHECK(binding.Texts->HasOld);
	CHECK(binding.Texts->Old == "gone");

	// The mirror image: an added file has no old path, so the two together are the pair that
	// only a key naming the file - rather than one side of it - keeps apart.
	Settle(binding, model, IdOf(1), AddedFile("fresh.cpp"));

	REQUIRE(binding.Texts != nullptr);
	CHECK(binding.Texts->HasNew);
	CHECK(binding.Texts->New == "fresh");
}

TEST_CASE("A renamed file's texts are keyed by its new path")
{
	FakePort port;
	port.TextsByPaths[FakePort::PathsKey("old.cpp", "new.cpp")] = TextsNaming("renamed");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	Settle(binding, model, IdOf(1), RenamedFile("old.cpp", "new.cpp"));

	REQUIRE(binding.Texts != nullptr);
	CHECK(binding.Key == std::filesystem::path{"new.cpp"});
	CHECK(model.GetTextsFor(IdOf(1), "new.cpp") != nullptr);
	CHECK(model.GetTextsFor(IdOf(1), "old.cpp") == nullptr);
}

TEST_CASE("A binary file's texts are never fetched")
{
	FakePort port;
	port.Texts = TextsNaming("bytes");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	Core::FileDiff file = ModifiedFile("image.png");
	file.IsBinary = true;

	Graphite::FileTextsBinding binding;

	Settle(binding, model, IdOf(1), file);

	// The pane can only ever say "Binary file - no textual diff.", so reading both blobs
	// would retain them for nothing, and nothing on this path caps how large they are.
	CHECK(port.TextCalls == 0);
	CHECK(binding.Texts == nullptr);
	CHECK(binding.Tokens.Old.empty());
	CHECK(binding.Tokens.New.empty());
	CHECK(model.GetTextsFor(IdOf(1), "image.png") == nullptr);
}

TEST_CASE("Turning to a binary file drops the texts the previous one left")
{
	FakePort port;
	port.Texts = TextsNaming("text");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	Settle(binding, model, IdOf(1), ModifiedFile("a.cpp"));
	REQUIRE(binding.Texts != nullptr);

	Core::FileDiff binary = ModifiedFile("image.png");
	binary.IsBinary = true;

	Settle(binding, model, IdOf(1), binary);

	CHECK(binding.Texts == nullptr);
	CHECK(port.TextCalls == 1);
}

TEST_CASE("Moving to another file forgets the one just left, if its read failed")
{
	FakePort port;
	port.Texts = TextsNaming("a");
	port.FailTexts = true;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	Settle(binding, model, IdOf(1), ModifiedFile("a.cpp"));
	REQUIRE(binding.Texts == nullptr);
	REQUIRE(port.TextCalls == 1);

	// A failed entry suppresses its own re-request, so asking again while still on the same
	// file must not queue a second read.
	binding.Update(model, IdOf(1), ModifiedFile("a.cpp"));
	CHECK(port.TextCalls == 1);

	Settle(binding, model, IdOf(1), ModifiedFile("b.cpp"));

	port.FailTexts = false;

	// Returning to the first file asks again, because leaving it dropped its failed entry -
	// a transient read failure must not suppress a file forever.
	Settle(binding, model, IdOf(1), ModifiedFile("a.cpp"));

	REQUIRE(binding.Texts != nullptr);
	CHECK(binding.Texts->Old == "a");
}

TEST_CASE("Each side is lexed with its own path's language")
{
	FakePort port;
	port.TextsByPaths[FakePort::PathsKey("foo.txt", "foo.cpp")] = TextsNaming("int x;");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	Settle(binding, model, IdOf(1), RenamedFile("foo.txt", "foo.cpp"));

	REQUIRE(binding.Texts != nullptr);

	// A rename that changes the extension really is two languages. The old side was plain
	// text and has no lexer; lexing it with the new side's would claim it was C++.
	CHECK(binding.Tokens.Old.empty());
	CHECK_FALSE(binding.Tokens.New.empty());
}

TEST_CASE("The tokens are not rebuilt while the texts stay the same")
{
	FakePort port;
	port.Texts = TextsNaming("int x;");

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	Settle(binding, model, IdOf(1), ModifiedFile("a.cpp"));

	REQUIRE(binding.Texts != nullptr);
	REQUIRE_FALSE(binding.Tokens.New.empty());

	const Graphite::FileTexts* held = binding.Texts.get();
	const std::vector<Graphite::Token>* firstLine = binding.Tokens.New.data();

	binding.Update(model, IdOf(1), ModifiedFile("a.cpp"));
	binding.Update(model, IdOf(1), ModifiedFile("a.cpp"));

	// The text is immutable once fetched, so an unchanged pointer means the tokens are still
	// good. Re-lexing a whole file every frame is the waste LexFileText's header warns of.
	CHECK(binding.Texts.get() == held);
	CHECK(binding.Tokens.New.data() == firstLine);
}

TEST_CASE("A refresh asks about the history again, not only the status")
{
	// The bug this was written for: a commit made outside the application never appeared,
	// because a refresh re-queried the status and the branches and left the log alone.
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestLog({});
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(port.LogCalls == 1);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{{}, false});
	model.OnUpdate(Core::Timestep{0.2f});

	CHECK(port.LogCalls == 2);
}

TEST_CASE("A refresh repeats the query the view is showing, not a default one")
{
	// The reader's ref and filter are still typed into the boxes above the list, so a
	// refresh that asked a plain question would answer one nobody is looking at.
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	Core::LogRequest request;
	request.Ref = "refs/heads/topic";
	request.Filter.Text = "encoding";
	request.MaxCount = 40;

	model.RequestLog(request);
	model.OnUpdate(Core::Timestep{0.0f});

	events.Publish(Graphite::WorkspaceFilesChangedEvent{{}, false});
	model.OnUpdate(Core::Timestep{0.2f});

	// Checked as well as the contents, so this cannot pass by the refresh never happening
	// and LastLogRequest still holding the first query.
	REQUIRE(port.LogCalls == 2);

	REQUIRE(port.LastLogRequest.has_value());
	CHECK(port.LastLogRequest->Ref == request.Ref);
	CHECK(port.LastLogRequest->Filter.Text == "encoding");
	CHECK(port.LastLogRequest->MaxCount == 40);
}

TEST_CASE("A refresh before anything asked for a log asks for none")
{
	// Nothing is on screen to keep fresh. Asking anyway would walk the history for a panel
	// the reader may never open.
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{{}, false});
	model.OnUpdate(Core::Timestep{0.2f});

	CHECK(port.StatusCalls == 1);
	CHECK(port.LogCalls == 0);
}

TEST_CASE("Closing the workspace forgets which query to repeat")
{
	// Otherwise the first refresh in the next folder would re-ask the previous folder's
	// question, filter and ref and all.
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestLog({});
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(port.LogCalls == 1);

	events.Publish(Graphite::WorkspaceClosedEvent{});
	events.Publish(Graphite::WorkspaceOpenedEvent{"C:/other"});
	model.OnUpdate(Core::Timestep{0.2f});

	CHECK(port.LogCalls == 1);
}

TEST_CASE("A commit made outside the application refreshes what is on screen")
{
	// The working tree does not change when someone commits in a terminal, so no file-change
	// event follows it. Without hearing about the repository itself, the panel would keep
	// showing the commit list and the status from before.
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestLog({});
	model.OnUpdate(Core::Timestep{0.0f});
	REQUIRE(port.LogCalls == 1);

	events.Publish(Graphite::RepositoryChangedEvent{});
	model.OnUpdate(Core::Timestep{0.2f});

	CHECK(port.StatusCalls == 1);
	CHECK(port.BranchCalls == 1);
	CHECK(port.LogCalls == 2);
}

TEST_CASE("A repository change outside a workspace refreshes nothing")
{
	// Mirrors the file-change rule beside it: nothing on screen has any business asking
	// about a folder that is not open.
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, Inline()};
	model.SubscribeTo(events);

	events.Publish(Graphite::RepositoryChangedEvent{});
	model.OnUpdate(Core::Timestep{0.2f});

	CHECK(port.StatusCalls == 0);
}

TEST_CASE("A working-tree diff brings its file's text with it")
{
	FakePort port;

	// Renamed, so the two sides carry different paths and a test can tell which one each
	// side was read under.
	Core::FileDiff file;
	file.OldPath = "old.txt";
	file.NewPath = "new.txt";
	port.Diff.Files.push_back(file);

	port.WorkingTreeTexts.HasNew = true;
	port.WorkingTreeTexts.New = "on disk";

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	CHECK(model.GetWorkingTreeTexts() == nullptr);

	model.RequestWorkingTreeDiff({.Target = Core::DiffTarget::WorkTreeToIndex, .File = "new.txt"});
	model.OnUpdate(Core::Timestep{0.0f});

	REQUIRE(model.GetWorkingTreeTexts() != nullptr);
	CHECK(model.GetWorkingTreeTexts()->New == "on disk");
	CHECK(port.WorkingTreeTextCalls == 1);
	CHECK(port.LastTextPaths == "old.txt|new.txt");
}

TEST_CASE("A working-tree diff that names no file reads no text")
{
	FakePort port;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestWorkingTreeDiff({.Target = Core::DiffTarget::WorkTreeToIndex, .File = "gone.txt"});
	model.OnUpdate(Core::Timestep{0.0f});

	CHECK(port.WorkingTreeTextCalls == 0);
	CHECK(model.GetWorkingTreeTexts() == nullptr);
}

TEST_CASE("An untracked listing is null until it has been asked for and applied")
{
	FakePort port;
	Core::WorkingTreeStatus listing;
	Core::FileStatus file;
	file.Path = "notes/first.md";
	file.IsUntracked = true;
	listing.Entries.push_back(file);
	port.UntrackedByDirectory[std::filesystem::path("notes")] = listing;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	CHECK(model.GetUntrackedIn("notes") == nullptr);

	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep(0.0f));

	const auto held = model.GetUntrackedIn("notes");
	REQUIRE(held != nullptr);
	CHECK(held->Entries.size() == 1);
	CHECK(port.LastUntrackedDirectory == std::filesystem::path("notes"));
}

TEST_CASE("A listing already held is not asked for again")
{
	FakePort port;
	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep(0.0f));
	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep(0.0f));

	CHECK(port.UntrackedCalls == 1);
}

TEST_CASE("A listing is dropped when the working tree changes")
{
	FakePort port;
	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep(0.0f));
	REQUIRE(model.GetUntrackedIn("notes") != nullptr);

	model.RequestStatus();
	model.OnUpdate(Core::Timestep(0.0f));

	CHECK(model.GetUntrackedIn("notes") == nullptr);
}

TEST_CASE("A failed listing answers null and is not retried on its own")
{
	FakePort port;
	port.FailUntracked = true;

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep(0.0f));
	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep(0.0f));

	CHECK(model.GetUntrackedIn("notes") == nullptr);
	CHECK(port.UntrackedCalls == 1);
}

TEST_CASE("Clearing the model forgets every listing")
{
	FakePort port;
	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep(0.0f));
	REQUIRE(model.GetUntrackedIn("notes") != nullptr);

	model.Clear();

	CHECK(model.GetUntrackedIn("notes") == nullptr);
}

TEST_CASE("A requested head becomes readable after the next update")
{
	FakePort port;
	port.Head.Kind = Core::HeadKind::Branch;
	port.Head.BranchName = "main";

	Graphite::GitModel model{port, Inline()};

	model.RequestHead();
	CHECK(model.GetHead() == nullptr);

	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetHead() != nullptr);
	CHECK(model.GetHead()->BranchName == "main");
}

TEST_CASE("The head is refreshed whenever the status is")
{
	FakePort port;
	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	Core::EventBus events;
	model.SubscribeTo(events);
	events.Publish(Graphite::RepositoryChangedEvent{});

	model.OnUpdate(Core::Timestep{1.0f});

	CHECK(port.StatusCalls == 1);
	CHECK(port.HeadCalls == 1);
	CHECK(model.GetHead() != nullptr);
}

TEST_CASE("Closing the folder forgets the head")
{
	FakePort port;
	Graphite::GitModel model{port, Inline()};

	model.RequestHead();
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetHead() != nullptr);

	model.Clear();

	CHECK(model.GetHead() == nullptr);
	CHECK_FALSE(model.GetHeadError().has_value());
}

TEST_CASE("A failed head query keeps the last good head and reports itself")
{
	FakePort port;
	port.Head.BranchName = "main";
	Graphite::GitModel model{port, Inline()};

	model.RequestHead();
	model.OnUpdate(Core::Timestep{0.016f});

	port.FailHead = true;
	model.RequestHead();
	model.OnUpdate(Core::Timestep{0.016f});

	REQUIRE(model.GetHead() != nullptr);
	CHECK(model.GetHead()->BranchName == "main");
	CHECK(model.GetHeadError().has_value());
}

TEST_CASE("Stashes are refreshed with the branches and forgotten on close")
{
	FakePort port;
	Core::StashEntry entry;
	entry.Message = "On main: wip";
	port.Stashes = {entry};

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	Core::EventBus events;
	model.SubscribeTo(events);
	events.Publish(Graphite::RepositoryChangedEvent{});
	model.OnUpdate(Core::Timestep{1.0f});

	CHECK(port.StashCalls == 1);
	REQUIRE(model.GetStashes() != nullptr);
	CHECK(model.GetStashes()->size() == 1);

	model.Clear();
	CHECK(model.GetStashes() == nullptr);
}

TEST_CASE("A stash's diff is asked for as a stash's, so its untracked files are in it")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Core::StashEntry entry;
	entry.Message = "On main: wip";
	entry.Id = IdOf(7);
	port.Stashes = {entry};

	Graphite::GitModel model{port, Inline()};
	model.SetRoot("C:/repo");

	Core::EventBus events;
	model.SubscribeTo(events);
	events.Publish(Graphite::RepositoryChangedEvent{});
	model.OnUpdate(Core::Timestep{1.0f});

	REQUIRE(model.GetStashes() != nullptr);

	model.RequestDiffFor(IdOf(7));
	model.RequestDiffFor(IdOf(1));
	model.OnUpdate(Core::Timestep{0.016f});

	CHECK(port.DiffTargets ==
		  std::vector<Core::DiffTarget>{Core::DiffTarget::StashToParent, Core::DiffTarget::CommitToParent});
}
