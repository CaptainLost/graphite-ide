#include <doctest/doctest.h>

#include "Git/Worktrees/WorktreeChanges.h"

#include "DeferredJobs.h"
#include "GitFixtures.h"
#include "TempDirectory.h"

#include "Core/Git/Head.h"
#include "Git/Debouncer.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using Graphite::WorktreeChanges;

namespace {

	const Core::Timestep kFrame{0.016f};
	const int kFramesInBurst = 5;

	const std::filesystem::path kWorktree{"D:/work/eager-scribbling-frog"};
	const std::filesystem::path kOtherWorktree{"D:/work/other-tree"};

	struct WatchControl
	{
		bool Changed = false;
		bool Fails = false;
		int Live = 0;
		std::vector<std::filesystem::path> Started;
	};

	class FakeWatch final : public Graphite::WorktreeWatch
	{
	public:
		explicit FakeWatch(WatchControl* control) : m_Control(control) { ++m_Control->Live; }
		~FakeWatch() override { --m_Control->Live; }

		FakeWatch(const FakeWatch&) = delete;
		FakeWatch& operator=(const FakeWatch&) = delete;

		bool TakeChanged() override { return m_Control->Changed; }

	private:
		WatchControl* m_Control = nullptr;
	};

	Graphite::WorktreeWatchFactory FakeWatches(WatchControl& control)
	{
		WatchControl* controlled = &control;

		return [controlled](const std::filesystem::path& worktree) -> Core::Scope<Graphite::WorktreeWatch> {
			controlled->Started.push_back(worktree);

			if (controlled->Fails)
			{
				return nullptr;
			}

			return Core::CreateScope<FakeWatch>(controlled);
		};
	}

	// A worktree's status and HEAD as a test sets them, counting what the model reads.
	class WorktreePortDouble final : public EmptyGitPort
	{
	public:
		Core::Result<Core::WorkingTreeStatus> QueryStatus() override
		{
			++StatusCalls;
			return Status;
		}

		Core::Result<Core::HeadState> QueryHead() override
		{
			++HeadCalls;
			return Head;
		}

		Core::Result<Core::Diff> QueryDiff(const Core::DiffRequest&) override
		{
			++DiffCalls;
			return Core::Diff{};
		}

		Core::Result<Core::Oid> Commit(const Core::CommitRequest&) override { return OidWithFirstByte(9); }

		Core::Result<void> Stage(const Paths&) override
		{
			if (StageFailure.has_value())
			{
				return *StageFailure;
			}

			return Core::Result<void>::Ok();
		}

		std::optional<Core::Error> StageFailure;

		Core::WorkingTreeStatus Status;
		Core::HeadState Head;
		int StatusCalls = 0;
		int HeadCalls = 0;
		int DiffCalls = 0;
	};

	// The model, its port and its queues, as GraphiteLayer holds them, with the port's root recorded instead of set.
	struct Changes
	{
		WorktreePortDouble Port;
		WatchControl Watches;
		DeferredJobs Reads;
		DeferredJobs Writes;
		std::vector<std::filesystem::path> PortRoots;

		WorktreeChanges Model{Port, [this](const std::filesystem::path& root) { PortRoots.push_back(root); },
							  Reads.Submitter(), Writes.Submitter(), FakeWatches(Watches)};

		void Settle()
		{
			Reads.RunAll();
			Writes.RunAll();
			Model.OnUpdate(kFrame);
		}

		void ChangeWorktreeAndWaitOutDebounce()
		{
			Watches.Changed = true;

			for (int frame = 0; frame < kFramesInBurst; ++frame)
			{
				Model.OnUpdate(kFrame);
			}

			Watches.Changed = false;
			Model.OnUpdate(Core::Timestep{Graphite::Debouncer::kQuietSeconds});
		}
	};

	Core::FileStatus Staged(const char* path)
	{
		Core::FileStatus entry;
		entry.Path = path;
		entry.Index = Core::StatusFlags::Modified;
		return entry;
	}

	Core::FileStatus NotStaged(const char* path)
	{
		Core::FileStatus entry;
		entry.Path = path;
		entry.WorkTree = Core::StatusFlags::Modified;
		return entry;
	}
}

TEST_CASE("Worktree changes: following a worktree roots the model and the port at it and watches it once")
{
	Changes changes;

	changes.Model.Follow(kWorktree, true);

	CHECK(changes.PortRoots == std::vector<std::filesystem::path>{kWorktree});
	CHECK(changes.Model.Worktree() == kWorktree);
	CHECK(changes.Model.Model().HasOpenFolder());
	CHECK(changes.Watches.Started == std::vector<std::filesystem::path>{kWorktree});
	CHECK(changes.Watches.Live == 1);
	CHECK(changes.Model.IsWatching());

	changes.Model.Follow(kWorktree, true);
	changes.Settle();

	CHECK(changes.PortRoots.size() == 1);
	CHECK(changes.Watches.Started.size() == 1);

	changes.Model.Follow(kOtherWorktree, true);

	CHECK(changes.PortRoots == std::vector<std::filesystem::path>{kWorktree, kOtherWorktree});
	CHECK(changes.Watches.Started == std::vector<std::filesystem::path>{kWorktree, kOtherWorktree});
	CHECK(changes.Watches.Live == 1);
	CHECK(changes.Model.Worktree() == kOtherWorktree);

	changes.Model.Follow(std::nullopt, true);

	CHECK(changes.PortRoots == std::vector<std::filesystem::path>{kWorktree, kOtherWorktree, std::filesystem::path{}});
	CHECK(changes.Watches.Live == 0);
	CHECK_FALSE(changes.Model.IsWatching());
	CHECK_FALSE(changes.Model.Worktree().has_value());
	CHECK_FALSE(changes.Model.Model().HasOpenFolder());
}

TEST_CASE("Worktree changes: a worktree followed off screen is neither watched nor read until the window is back")
{
	Changes changes;

	changes.Model.Follow(kWorktree, false);
	changes.Settle();

	CHECK(changes.Model.Worktree() == kWorktree);
	CHECK(changes.Watches.Live == 0);
	CHECK(changes.Watches.Started.empty());
	CHECK_FALSE(changes.Model.IsWatching());
	CHECK(changes.Port.StatusCalls == 0);

	changes.Model.Follow(kWorktree, true);
	changes.Settle();

	CHECK(changes.Watches.Live == 1);
	CHECK(changes.Port.StatusCalls == 1);
	CHECK(changes.Port.HeadCalls == 1);

	changes.Model.Follow(kWorktree, false);

	CHECK(changes.Watches.Live == 0);
	CHECK_FALSE(changes.Model.IsWatching());
	CHECK(changes.PortRoots.size() == 1);
}

TEST_CASE("Worktree changes: a burst of watch changes asks one status after the debounce, not one per change")
{
	Changes changes;
	changes.Model.Follow(kWorktree, true);
	changes.Settle();
	const int statusCallsBefore = changes.Port.StatusCalls;

	changes.Watches.Changed = true;

	for (int frame = 0; frame < kFramesInBurst; ++frame)
	{
		changes.Model.OnUpdate(kFrame);
		changes.Reads.RunAll();
	}

	CHECK(changes.Port.StatusCalls == statusCallsBefore);

	changes.Watches.Changed = false;
	changes.Model.OnUpdate(Core::Timestep{Graphite::Debouncer::kQuietSeconds});
	changes.Settle();

	CHECK(changes.Port.StatusCalls == statusCallsBefore + 1);

	changes.Model.OnUpdate(Core::Timestep{Graphite::Debouncer::kMaxSeconds});
	changes.Settle();

	CHECK(changes.Port.StatusCalls == statusCallsBefore + 1);
}

TEST_CASE("Worktree changes: the diff on screen is read again after a watch change")
{
	Changes changes;
	changes.Model.Follow(kWorktree, true);
	changes.Settle();

	const Core::DiffRequest request{.File = std::filesystem::path{"a.txt"}};
	changes.Model.Model().RequestWorkingTreeDiff(request);
	changes.Settle();
	changes.Model.Model().RequestWorkingTreeDiff(request);
	changes.Settle();

	REQUIRE(changes.Port.DiffCalls == 1);

	changes.ChangeWorktreeAndWaitOutDebounce();
	changes.Model.Model().RequestWorkingTreeDiff(request);
	changes.Settle();

	CHECK(changes.Port.DiffCalls == 2);
}

TEST_CASE("Worktree changes: each worktree keeps its own draft through following another and back")
{
	Changes changes;

	changes.Model.Follow(kWorktree, true);
	changes.Model.Draft().Message = "Message for the frog";
	changes.Model.Draft().Amend = true;

	changes.Model.Follow(kOtherWorktree, true);

	CHECK(changes.Model.Draft().Message.empty());
	CHECK_FALSE(changes.Model.Draft().Amend);
	changes.Model.Draft().Message = "Message for the other tree";

	changes.Model.Follow(kWorktree, true);

	CHECK(changes.Model.Draft().Message == "Message for the frog");
	CHECK(changes.Model.Draft().Amend);

	changes.Model.Follow(kOtherWorktree, false);

	CHECK(changes.Model.Draft().Message == "Message for the other tree");
}

TEST_CASE("Worktree changes: a commit that lands clears the draft of its worktree only")
{
	Changes changes;
	changes.Model.Follow(kOtherWorktree, true);
	changes.Model.Draft().Message = "Kept";
	changes.Model.Follow(kWorktree, true);
	changes.Settle();
	changes.Model.Draft().Message = "Committed";

	changes.Model.Model().RequestCommit(Core::CommitRequest{.Message = "Committed"});
	changes.Settle();

	CHECK(changes.Model.Draft().Message.empty());

	changes.Model.Follow(kOtherWorktree, true);

	CHECK(changes.Model.Draft().Message == "Kept");
}

TEST_CASE("Worktree changes: a write in flight finishes in its own worktree before the model follows the next")
{
	Changes changes;
	changes.Model.Follow(kWorktree, true);
	changes.Settle();

	changes.Model.Model().RequestStage({"a.txt"});
	REQUIRE(changes.Model.Model().HasWritesPending());

	changes.Model.Follow(kOtherWorktree, true);

	CHECK(changes.PortRoots == std::vector<std::filesystem::path>{kWorktree});
	CHECK(changes.Model.Worktree() == kWorktree);

	changes.Settle();
	changes.Model.Follow(kOtherWorktree, true);

	CHECK(changes.PortRoots == std::vector<std::filesystem::path>{kWorktree, kOtherWorktree});
	CHECK(changes.Model.Worktree() == kOtherWorktree);
	CHECK(changes.Watches.Started == std::vector<std::filesystem::path>{kWorktree, kOtherWorktree});
}

TEST_CASE("Worktree changes: a write that fails while the switch to another worktree waits for it keeps its failure")
{
	Changes changes;
	changes.Port.StageFailure = Core::Error{Core::ErrorCode::IoFailure, "Git is busy in eager-scribbling-frog; try again"};
	changes.Model.Follow(kWorktree, true);
	changes.Settle();

	changes.Model.Model().RequestStage({"a.txt"});
	changes.Model.Follow(kOtherWorktree, true);
	changes.Settle();

	REQUIRE(changes.Model.Worktree() == kOtherWorktree);

	const std::optional<Graphite::WorktreeWriteFailure> failure = changes.Model.TakeWriteError();

	REQUIRE(failure.has_value());
	CHECK(failure->Failure.Kind == Graphite::GitModel::WriteKind::Stage);
	CHECK(failure->Failure.Error.Message == "Git is busy in eager-scribbling-frog; try again");

	// The failure names the worktree it happened in, not the one followed since.
	CHECK(failure->Worktree == kWorktree);
	CHECK_FALSE(changes.Model.TakeWriteError().has_value());
}

TEST_CASE("Worktree changes: the watch firing is told once per debounce, naming the worktree")
{
	Changes changes;
	std::vector<std::filesystem::path> fired;
	changes.Model.OnWatchFired = [&fired](const std::filesystem::path& worktree) { fired.push_back(worktree); };

	changes.Model.Follow(kWorktree, true);
	changes.Settle();

	CHECK(fired.empty());

	changes.ChangeWorktreeAndWaitOutDebounce();

	CHECK(fired == std::vector<std::filesystem::path>{kWorktree});

	changes.Model.OnUpdate(Core::Timestep{Graphite::Debouncer::kMaxSeconds});
	changes.ChangeWorktreeAndWaitOutDebounce();

	CHECK(fired == std::vector<std::filesystem::path>{kWorktree, kWorktree});
}

TEST_CASE("Worktree changes: the expectation carries the head, the HEAD commit, the sorted staged paths, the folder name and the folder")
{
	Changes changes;
	changes.Port.Head.Kind = Core::HeadKind::Branch;
	changes.Port.Head.BranchName = "worktree-eager-scribbling-frog";
	changes.Port.Head.Commit = OidWithFirstByte(7);
	changes.Port.Status = WorkingTreeStatusOf({Staged("src/b.cpp"), NotStaged("c.cpp"), Staged("a.cpp")});

	changes.Model.Follow(kWorktree, true);
	changes.Settle();

	const Graphite::WriteExpectation commit = changes.Model.ExpectationNow(true);

	CHECK(commit.Kind == Core::HeadKind::Branch);
	CHECK(commit.BranchName == "worktree-eager-scribbling-frog");
	CHECK(commit.Head == OidWithFirstByte(7));
	REQUIRE(commit.Staged.has_value());
	CHECK(*commit.Staged == std::vector<std::filesystem::path>{"a.cpp", "src/b.cpp"});
	CHECK(commit.Folder == "eager-scribbling-frog");
	CHECK(commit.Worktree == kWorktree);

	const Graphite::WriteExpectation stage = changes.Model.ExpectationNow(false);

	CHECK(stage.BranchName == "worktree-eager-scribbling-frog");
	CHECK(stage.Head == OidWithFirstByte(7));
	CHECK_FALSE(stage.Staged.has_value());
	CHECK(stage.Folder == "eager-scribbling-frog");
	CHECK(stage.Worktree == kWorktree);
}

TEST_CASE("Worktree changes: the expectation of a detached HEAD names no branch")
{
	Changes changes;
	changes.Port.Head.Kind = Core::HeadKind::Detached;
	changes.Port.Head.Commit = OidWithFirstByte(3);

	changes.Model.Follow(kWorktree, true);
	changes.Settle();

	const Graphite::WriteExpectation expectation = changes.Model.ExpectationNow(true);

	CHECK(expectation.Kind == Core::HeadKind::Detached);
	CHECK(expectation.BranchName.empty());
	CHECK(expectation.Head == OidWithFirstByte(3));
	REQUIRE(expectation.Staged.has_value());
	CHECK(expectation.Staged->empty());
}

TEST_CASE("Worktree changes: the worktree looked at is the one watched, so the counts leave it alone only while it is")
{
	Changes changes;

	CHECK_FALSE(changes.Model.LookedAt().has_value());

	changes.Model.Follow(kWorktree, true);

	CHECK(changes.Model.LookedAt() == kWorktree);

	changes.Model.Follow(kWorktree, false);

	CHECK_FALSE(changes.Model.LookedAt().has_value());

	changes.Watches.Fails = true;
	changes.Model.Follow(kOtherWorktree, true);

	CHECK_FALSE(changes.Model.IsWatching());
	CHECK_FALSE(changes.Model.LookedAt().has_value());
}

TEST_CASE("Worktree changes: a linked worktree's git directory is watched beside its folder")
{
	TempDirectory temp;
	const std::filesystem::path worktree = temp.MakeDirectory("frog");
	const std::filesystem::path gitDirectory = temp.MakeDirectory("repo/.git/worktrees/frog");

	{
		std::ofstream gitFile{worktree / ".git", std::ios::binary};
		gitFile << "gitdir: ../repo/.git/worktrees/frog\n";
	}

	Changes changes;
	changes.Model.Follow(worktree, true);

	REQUIRE(changes.Watches.Started.size() == 2);
	CHECK(changes.Watches.Started[0] == worktree);
	CHECK(Core::SamePath(changes.Watches.Started[1], gitDirectory));
	CHECK(changes.Watches.Live == 2);

	changes.Model.Follow(worktree, false);

	CHECK(changes.Watches.Live == 0);
}

TEST_CASE("Worktree changes: a git directory is read from a linked worktree's .git file, never from a .git folder")
{
	TempDirectory temp;
	const std::filesystem::path linked = temp.MakeDirectory("linked");
	const std::filesystem::path main = temp.MakeDirectory("main");
	temp.MakeDirectory("main/.git");
	const std::filesystem::path absolute = temp.GetPath() / "elsewhere" / "worktrees" / "linked";

	{
		std::ofstream gitFile{linked / ".git", std::ios::binary};
		gitFile << "gitdir: " << Core::ToUtf8(absolute) << "\r\n";
	}

	const std::optional<std::filesystem::path> named = Graphite::LinkedGitDirectoryOf(linked);

	REQUIRE(named.has_value());
	CHECK(Core::SamePath(*named, absolute));
	CHECK_FALSE(Graphite::LinkedGitDirectoryOf(main).has_value());
	CHECK_FALSE(Graphite::LinkedGitDirectoryOf(temp.GetPath() / "missing").has_value());
}

TEST_CASE("Worktree changes: a commit or an amend that lands says once that a ref moved, and a stage does not")
{
	Changes changes;
	changes.Model.Follow(kWorktree, true);
	changes.Settle();

	CHECK_FALSE(changes.Model.TakeRefMoved());

	changes.Model.Model().RequestStage({"a.txt"});
	changes.Settle();

	CHECK_FALSE(changes.Model.TakeRefMoved());

	for (const bool amend : {false, true})
	{
		CAPTURE(amend);

		changes.Model.Model().RequestCommit(Core::CommitRequest{.Message = "Committed", .Amend = amend});
		changes.Settle();

		CHECK(changes.Model.TakeRefMoved());
		CHECK_FALSE(changes.Model.TakeRefMoved());
	}
}

TEST_CASE("A linked worktree inside WSL whose .git names a Linux path has its git directory in the same distro")
{
	const std::optional<std::filesystem::path> named =
		Graphite::GitDirectoryNamedIn(L"\\\\wsl.localhost\\Ubuntu\\home\\me\\wt", "/home/me/p/.git/worktrees/wt");

	REQUIRE(named.has_value());
	CHECK(named->native() == std::wstring{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p\\.git\\worktrees\\wt"});
}

TEST_CASE("A relative gitdir is taken from the worktree, a local absolute one as it is")
{
	CHECK(Graphite::GitDirectoryNamedIn(L"\\\\wsl.localhost\\Ubuntu\\home\\me\\wt", "../p/.git/worktrees/wt")->native() == std::wstring{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p\\.git\\worktrees\\wt"});
	CHECK(Graphite::GitDirectoryNamedIn(L"C:\\w", "C:/r/.git/worktrees/w") == std::optional<std::filesystem::path>{std::filesystem::path{"C:/r/.git/worktrees/w"}.lexically_normal()});
}
