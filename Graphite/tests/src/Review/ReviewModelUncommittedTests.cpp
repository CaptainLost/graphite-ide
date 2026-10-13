#include <doctest/doctest.h>

#include "Review/ReviewModel.h"

#include "DeferredJobs.h"
#include "Git/Debouncer.h"
#include "Review/FakeReviewPort.h"

#include <filesystem>
#include <vector>

using Graphite::ReviewKey;
using Graphite::ReviewModel;

namespace {

	const Core::Timestep kFrame{0.016f};
	const Core::Timestep kTenthOfASecond{0.1f};

	const ReviewKey kKey{.Branch = "refs/heads/side", .Base = "refs/heads/main"};
	const std::filesystem::path kWorktree{"D:/work/side-tree"};
	const std::filesystem::path kOtherWorktree{"D:/work/other-tree"};

	struct WatchControl
	{
		bool Changed = false;
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

	Core::Scope<Graphite::WorktreeWatch> StartFakeWatch(WatchControl* control, const std::filesystem::path& worktree)
	{
		control->Started.push_back(worktree);
		return Core::CreateScope<FakeWatch>(control);
	}

	Graphite::WorktreeWatchFactory FakeWatches(WatchControl& control)
	{
		WatchControl* controlled = &control;
		return [controlled](const std::filesystem::path& worktree) { return StartFakeWatch(controlled, worktree); };
	}

	Core::FileDiff TextFile(const std::filesystem::path& path)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.OldPath = path;
		file.NewPath = path;
		return file;
	}

	void Settle(ReviewModel& model, DeferredJobs& jobs)
	{
		jobs.RunAll();
		model.OnUpdate(kFrame);
	}

	void AskAndSettle(ReviewModel& model, DeferredJobs& jobs, const std::filesystem::path& worktree)
	{
		model.RequestUncommitted(kKey, worktree);
		Settle(model, jobs);
	}
}

TEST_CASE("The uncommitted diff is asked once and held while the worktree is quiet")
{
	FakeReviewPort port;
	port.WorktreeDiff.Files.push_back(TextFile("a.cpp"));
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};

	for (int frame = 0; frame < 10; ++frame)
	{
		AskAndSettle(model, jobs, kWorktree);
	}

	CHECK(port.WorktreeDiffQueries == 1);
	CHECK(watches.Started == std::vector<std::filesystem::path>{kWorktree});
	REQUIRE(model.GetUncommitted(kKey) != nullptr);
	CHECK(model.GetUncommitted(kKey)->Files.size() == 1);
}

TEST_CASE("A change in the worktree asks for the uncommitted diff again once the debounce runs out")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	AskAndSettle(model, jobs, kWorktree);

	watches.Changed = true;
	model.OnUpdate(kFrame);
	watches.Changed = false;
	model.OnUpdate(Core::Timestep{Graphite::Debouncer::kQuietSeconds});
	AskAndSettle(model, jobs, kWorktree);

	CHECK(port.WorktreeDiffQueries == 2);
}

TEST_CASE("A worktree that keeps changing is asked about once per debounce burst")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	AskAndSettle(model, jobs, kWorktree);

	watches.Changed = true;

	for (int frame = 0; frame < 12; ++frame)
	{
		model.OnUpdate(kTenthOfASecond);
		model.RequestUncommitted(kKey, kWorktree);
		jobs.RunAll();
	}

	CHECK(port.WorktreeDiffQueries == 2);
}

TEST_CASE("A branch moving marks the uncommitted diff stale too")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	AskAndSettle(model, jobs, kWorktree);

	model.InvalidateRanges();
	AskAndSettle(model, jobs, kWorktree);

	CHECK(port.WorktreeDiffQueries == 2);
}

TEST_CASE("Turning the toggle off stops the watch and drops the answer")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	AskAndSettle(model, jobs, kWorktree);
	CHECK(watches.Live == 1);

	model.StopUncommitted(kKey);

	CHECK(watches.Live == 0);
	CHECK(model.GetUncommitted(kKey) == nullptr);
}

TEST_CASE("A different worktree starts over with a watch of its own")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	AskAndSettle(model, jobs, kWorktree);

	AskAndSettle(model, jobs, kOtherWorktree);

	CHECK(watches.Started == std::vector<std::filesystem::path>{kWorktree, kOtherWorktree});
	CHECK(watches.Live == 1);
	CHECK(port.WorktreeDiffQueries == 2);
}

TEST_CASE("Uncommitted texts belong to the diff they were read beside")
{
	FakeReviewPort port;
	port.WorktreeDiff.Files.push_back(TextFile("a.cpp"));
	port.Texts.New = "edited\n";
	port.Texts.HasNew = true;
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	const Core::FileDiff file = TextFile("a.cpp");
	AskAndSettle(model, jobs, kWorktree);

	model.RequestUncommittedTexts(kKey, file);
	Settle(model, jobs);
	REQUIRE(model.GetUncommittedTexts(kKey, file) != nullptr);
	CHECK(model.GetUncommittedTexts(kKey, file)->New == "edited\n");

	model.InvalidateRanges();
	AskAndSettle(model, jobs, kWorktree);
	CHECK(model.GetUncommittedTexts(kKey, file) == nullptr);

	model.RequestUncommittedTexts(kKey, file);
	Settle(model, jobs);
	CHECK(model.GetUncommittedTexts(kKey, file) != nullptr);
	CHECK(port.WorktreeTextReads == 2);
}

TEST_CASE("A failed uncommitted diff is asked again only after a retry")
{
	FakeReviewPort port;
	port.WorktreeFailure = Core::Error{Core::ErrorCode::NotFound, "the worktree is gone"};
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	AskAndSettle(model, jobs, kWorktree);
	AskAndSettle(model, jobs, kWorktree);

	REQUIRE(model.GetUncommittedError(kKey) != nullptr);
	CHECK(port.WorktreeDiffQueries == 1);

	port.WorktreeFailure.reset();
	model.Retry(kKey);
	AskAndSettle(model, jobs, kWorktree);

	CHECK(port.WorktreeDiffQueries == 2);
	CHECK(model.GetUncommittedError(kKey) == nullptr);
}

TEST_CASE("Forgetting a review stops its watch too")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	AskAndSettle(model, jobs, kWorktree);

	model.Forget(kKey);

	CHECK(watches.Live == 0);
}
