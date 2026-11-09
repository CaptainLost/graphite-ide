#include <doctest/doctest.h>

#include "Git/Worktrees/LookedAtWorktree.h"

#include "DeferredJobs.h"
#include "GitFixtures.h"
#include "WorktreeFixtures.h"

#include "Git/Debouncer.h"
#include "Git/Worktrees/WorktreeChanges.h"
#include "Git/Worktrees/WorktreesModel.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vector>

using Graphite::Fixtures::FakeWorktreePort;
using Graphite::Fixtures::WorktreeOn;

namespace {

	const std::filesystem::path kOpen{"D:/repo"};
	const std::filesystem::path kLookedAt{"D:/repo-frog"};
	const std::filesystem::path kOther{"D:/repo-other"};
	const Core::Timestep kFrame{0.016f};
	const Core::Timestep kSecond{1.0f};
	const int kSecondsWatched = 60;

	class SwitchedWatch final : public Graphite::WorktreeWatch
	{
	public:
		explicit SwitchedWatch(const bool* changed) : m_Changed(changed) {}

		bool TakeChanged() override { return *m_Changed; }

	private:
		const bool* m_Changed = nullptr;
	};

	// The looked-at worktree's repository: its HEAD as a test sets it, on frog as the list has it at first.
	class HeadPort final : public EmptyGitPort
	{
	public:
		HeadPort()
		{
			Head.Kind = Core::HeadKind::Branch;
			Head.BranchName = "frog";
		}

		Core::Result<Core::HeadState> QueryHead() override { return Head; }

		Core::HeadState Head;
	};

	// The Git window's worktree counts and the looked-at worktree's changes, tied as GraphiteLayer ties them.
	struct Window
	{
		FakeWorktreePort CountPort;
		DeferredJobs CountJobs;
		Graphite::WorktreesModel Counts{CountPort, CountJobs.Submitter(), [] { return std::int64_t{0}; }};

		HeadPort ChangesPort;
		DeferredJobs ChangesJobs;
		bool WatchChanged = false;
		Graphite::WorktreeChanges Changes{ChangesPort, [](const std::filesystem::path&) {}, ChangesJobs.Submitter(),
										  ChangesJobs.Submitter(), [this](const std::filesystem::path&) {
											  return Core::CreateScope<SwitchedWatch>(&WatchChanged);
										  }};

		Graphite::LookedAtWorktree LookedAt{Changes, Counts};

		Window()
		{
			CountPort.Entries = {WorktreeOn("main", kOpen), WorktreeOn("frog", kLookedAt), WorktreeOn("other", kOther)};
			Counts.SetOnScreen(true);
			Counts.Follow(Core::CreateRef<const std::vector<Core::Branch>>(), kOpen);
			CountJobs.RunAll();
			Counts.OnUpdate(Core::Timestep{0.0f});
			CountJobs.RunAll();
			Counts.OnUpdate(Core::Timestep{0.0f});
		}

		// A second on screen with every worktree's row drawn.
		void DrawRowsFor(Core::Timestep seconds)
		{
			Counts.DirtyInView(kLookedAt);
			Counts.DirtyInView(kOther);
			Counts.OnUpdate(seconds);
			CountJobs.RunAll();
			Counts.OnUpdate(Core::Timestep{0.0f});
			ChangesJobs.RunAll();
			LookedAt.Update(kFrame);
		}

		long CountsOf(const std::filesystem::path& worktree) const
		{
			return static_cast<long>(std::ranges::count(CountPort.Counted, worktree));
		}
	};
}

TEST_CASE("Looked-at worktree: the counts are told the worktree watched and leave its polling to the watch")
{
	Window window;
	window.Changes.Follow(kLookedAt, true);
	window.LookedAt.Update(kFrame);
	window.CountPort.Counted.clear();

	for (int second = 0; second < kSecondsWatched; ++second)
	{
		window.DrawRowsFor(kSecond);
	}

	CHECK(window.CountsOf(kLookedAt) == 0);
	CHECK(window.CountsOf(kOther) >= 2);

	window.Changes.Follow(kLookedAt, false);
	window.LookedAt.Update(kFrame);

	for (int second = 0; second < kSecondsWatched; ++second)
	{
		window.DrawRowsFor(kSecond);
	}

	CHECK(window.CountsOf(kLookedAt) >= 1);
}

TEST_CASE("Looked-at worktree: a watch change counts the worktree watched once, after the debounce")
{
	Window window;
	window.Changes.Follow(kLookedAt, true);
	window.LookedAt.Update(kFrame);
	window.CountPort.Counted.clear();

	window.WatchChanged = true;
	window.LookedAt.Update(kFrame);
	window.LookedAt.Update(kFrame);
	window.CountJobs.RunAll();

	CHECK(window.CountsOf(kLookedAt) == 0);

	window.WatchChanged = false;
	window.LookedAt.Update(Core::Timestep{Graphite::Debouncer::kQuietSeconds});
	window.CountJobs.RunAll();

	CHECK(window.CountsOf(kLookedAt) == 1);
	CHECK(window.CountsOf(kOther) == 0);
}

TEST_CASE("Looked-at worktree: a watch change that finds HEAD detached asks the worktree list again once, and one of files alone does not")
{
	Window window;
	window.Changes.Follow(kLookedAt, true);
	window.LookedAt.Update(kFrame);
	window.ChangesJobs.RunAll();
	window.LookedAt.Update(kFrame);
	window.CountJobs.RunAll();
	const int listedBefore = window.CountPort.Listed;

	const auto fireWatch = [&window] {
		window.WatchChanged = true;
		window.LookedAt.Update(kFrame);
		window.WatchChanged = false;
		window.LookedAt.Update(Core::Timestep{Graphite::Debouncer::kQuietSeconds});
		window.ChangesJobs.RunAll();
		window.LookedAt.Update(kFrame);
		window.LookedAt.Update(kFrame);
		window.CountJobs.RunAll();
	};

	// A file changed: HEAD is where the list has it.
	fireWatch();
	CHECK(window.CountPort.Listed == listedBefore);

	// Another process detached HEAD: the list is read again, once however many frames follow.
	window.ChangesPort.Head.Kind = Core::HeadKind::Detached;
	window.ChangesPort.Head.BranchName.clear();
	window.ChangesPort.Head.Commit = OidWithFirstByte(0x5A);
	fireWatch();
	window.LookedAt.Update(kFrame);
	window.CountJobs.RunAll();

	CHECK(window.CountPort.Listed == listedBefore + 1);
}
