#include <doctest/doctest.h>

#include "Review/ReviewModel.h"

#include "DeferredJobs.h"
#include "Git/Debouncer.h"
#include "Review/FakeReviewPort.h"

#include <cstdint>
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

	Graphite::ReviewRange RangeWithTip(std::uint8_t tipNumber)
	{
		Graphite::ReviewRange range;
		range.Ends.MergeBase.Bytes[0] = 1;
		range.Ends.Tip.Bytes[0] = tipNumber;
		return range;
	}

	void Settle(ReviewModel& model, DeferredJobs& jobs)
	{
		jobs.RunAll();
		model.OnUpdate(kFrame);
	}

	void AskAndSettle(ReviewModel& model, DeferredJobs& jobs, const std::filesystem::path& worktree)
	{
		model.RequestUncommitted(kKey, worktree, Core::WhitespaceInDiff::Shown);
		Settle(model, jobs);
	}

	void ResolveRangeAndSettle(ReviewModel& model, DeferredJobs& jobs)
	{
		model.RequestRange(kKey);
		Settle(model, jobs);
	}

	// A change under the worktree that has settled past the debounce.
	void ChangeWorktree(ReviewModel& model, WatchControl& watches)
	{
		watches.Changed = true;
		model.OnUpdate(kFrame);
		watches.Changed = false;
		model.OnUpdate(Core::Timestep{Graphite::Debouncer::kQuietSeconds});
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
		model.RequestUncommitted(kKey, kWorktree, Core::WhitespaceInDiff::Shown);
		jobs.RunAll();
	}

	CHECK(port.WorktreeDiffQueries == 2);
}

TEST_CASE("A branch moving marks the uncommitted diff stale too")
{
	FakeReviewPort port;
	port.RangeAnswers = {RangeWithTip(2), RangeWithTip(3)};
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	ResolveRangeAndSettle(model, jobs);
	AskAndSettle(model, jobs, kWorktree);

	model.InvalidateRanges();
	ResolveRangeAndSettle(model, jobs);
	AskAndSettle(model, jobs, kWorktree);

	CHECK(port.WorktreeDiffQueries == 2);
}

TEST_CASE("A branch snapshot that leaves the tip where it was keeps the uncommitted diff")
{
	FakeReviewPort port;
	port.RangeAnswers = {RangeWithTip(2), RangeWithTip(2)};
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	ResolveRangeAndSettle(model, jobs);
	AskAndSettle(model, jobs, kWorktree);

	model.InvalidateRanges();
	ResolveRangeAndSettle(model, jobs);
	AskAndSettle(model, jobs, kWorktree);

	CHECK(port.RangeQueries == 2);
	CHECK(port.WorktreeDiffQueries == 1);
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

	port.WorktreeDiff.Files[0].NewId.Bytes[0] = 1;
	ChangeWorktree(model, watches);
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

TEST_CASE("Uncommitted texts survive a refresh that leaves their file as it was")
{
	FakeReviewPort port;
	port.WorktreeDiff.Files.push_back(TextFile("a.cpp"));
	port.Texts.HasNew = true;
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	const Core::FileDiff file = TextFile("a.cpp");
	AskAndSettle(model, jobs, kWorktree);
	model.RequestUncommittedTexts(kKey, file);
	Settle(model, jobs);

	ChangeWorktree(model, watches);
	AskAndSettle(model, jobs, kWorktree);
	model.RequestUncommittedTexts(kKey, file);

	CHECK(port.WorktreeDiffQueries == 2);
	CHECK(model.GetUncommittedTexts(kKey, file) != nullptr);
	CHECK(port.WorktreeTextReads == 1);
}

TEST_CASE("Uncommitted texts are read again when the refresh changes their file")
{
	FakeReviewPort port;
	port.WorktreeDiff.Files.push_back(TextFile("a.cpp"));
	port.Texts.HasNew = true;
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	const Core::FileDiff file = TextFile("a.cpp");
	AskAndSettle(model, jobs, kWorktree);
	model.RequestUncommittedTexts(kKey, file);
	Settle(model, jobs);

	port.WorktreeDiff.Files[0].NewId.Bytes[0] = 1;
	ChangeWorktree(model, watches);
	AskAndSettle(model, jobs, kWorktree);
	CHECK(model.GetUncommittedTexts(kKey, file) == nullptr);
	model.RequestUncommittedTexts(kKey, file);
	Settle(model, jobs);

	CHECK(model.GetUncommittedTexts(kKey, file) != nullptr);
	CHECK(port.WorktreeTextReads == 2);
}

TEST_CASE("Asking for the same uncommitted texts every frame reads them once")
{
	FakeReviewPort port;
	port.WorktreeDiff.Files.push_back(TextFile("a.cpp"));
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	const Core::FileDiff file = TextFile("a.cpp");
	AskAndSettle(model, jobs, kWorktree);

	for (int frame = 0; frame < 10; ++frame)
	{
		model.RequestUncommittedTexts(kKey, file);
		Settle(model, jobs);
	}

	CHECK(port.WorktreeTextReads == 1);
}

TEST_CASE("A diff still on its way when the review moves to another worktree does not land there")
{
	FakeReviewPort port;
	port.WorktreeDiffsByPath[kWorktree].Files.push_back(TextFile("old.cpp"));
	port.WorktreeDiffsByPath[kOtherWorktree].Files.push_back(TextFile("new.cpp"));
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	model.RequestUncommitted(kKey, kWorktree, Core::WhitespaceInDiff::Shown);
	model.RequestUncommitted(kKey, kOtherWorktree, Core::WhitespaceInDiff::Shown);

	Settle(model, jobs);

	REQUIRE(model.GetUncommitted(kKey) != nullptr);
	REQUIRE(model.GetUncommitted(kKey)->Files.size() == 1);
	CHECK(model.GetUncommitted(kKey)->Files[0].NewPath == std::filesystem::path{"new.cpp"});
}

TEST_CASE("Changing the white space setting asks for the uncommitted diff again")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	AskAndSettle(model, jobs, kWorktree);

	model.RequestUncommitted(kKey, kWorktree, Core::WhitespaceInDiff::Ignored);
	Settle(model, jobs);

	CHECK(port.WorktreeDiffQueries == 2);
	CHECK(port.LastWorktreeWhitespace == Core::WhitespaceInDiff::Ignored);
}

TEST_CASE("A failed uncommitted text read is tried again after a refresh that leaves the file unchanged")
{
	FakeReviewPort port;
	port.WorktreeDiff.Files.push_back(TextFile("a.cpp"));
	port.WorktreeTextsFailure = Core::Error{Core::ErrorCode::IoFailure, "locked"};
	DeferredJobs jobs;
	WatchControl watches;
	ReviewModel model{port, jobs.Submitter(), FakeWatches(watches)};
	const Core::FileDiff file = TextFile("a.cpp");
	AskAndSettle(model, jobs, kWorktree);
	model.RequestUncommittedTexts(kKey, file);
	Settle(model, jobs);
	CHECK(model.GetUncommittedTexts(kKey, file) == nullptr);

	port.WorktreeTextsFailure.reset();
	model.Retry(kKey);
	AskAndSettle(model, jobs, kWorktree);
	model.RequestUncommittedTexts(kKey, file);
	Settle(model, jobs);

	CHECK(port.WorktreeTextReads == 2);
	CHECK(model.GetUncommittedTexts(kKey, file) != nullptr);
}
