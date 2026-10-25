#include <doctest/doctest.h>

#include "Review/ReviewModel.h"

#include "DeferredJobs.h"
#include "Review/FakeReviewPort.h"

#include <string>

using Graphite::RangeDiffKey;
using Graphite::RangeEnds;
using Graphite::ReviewKey;
using Graphite::ReviewModel;
using Graphite::ReviewRange;

namespace {

	const Core::Timestep kFrame{0.016f};

	const ReviewKey kKey{.Branch = "refs/heads/side", .Base = "refs/heads/main"};

	Core::Oid IdNumbered(std::uint8_t number)
	{
		Core::Oid id;
		id.Bytes[0] = number;
		return id;
	}

	ReviewRange RangeWithTip(std::uint8_t tipNumber)
	{
		ReviewRange range;
		range.Ends = RangeEnds{.MergeBase = IdNumbered(1), .Tip = IdNumbered(tipNumber)};
		return range;
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
}

TEST_CASE("A review's range is resolved once and held")
{
	FakeReviewPort port;
	port.Range = RangeWithTip(2);
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};

	model.RequestRange(kKey);
	CHECK(model.IsRangeLoading(kKey));
	Settle(model, jobs);
	model.RequestRange(kKey);
	Settle(model, jobs);

	CHECK(port.RangeQueries == 1);
	REQUIRE(model.GetRange(kKey) != nullptr);
	CHECK(model.GetRange(kKey)->Ends.Tip == IdNumbered(2));
	CHECK_FALSE(model.IsRangeLoading(kKey));
}

TEST_CASE("A range is not asked twice while its answer is in flight")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};

	model.RequestRange(kKey);
	model.RequestRange(kKey);

	CHECK(jobs.Count() == 1);
}

TEST_CASE("Invalidating the ranges asks again on the next request")
{
	FakeReviewPort port;
	port.RangeAnswers = {RangeWithTip(2), RangeWithTip(3)};
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	model.RequestRange(kKey);
	Settle(model, jobs);

	model.InvalidateRanges();
	model.RequestRange(kKey);
	Settle(model, jobs);

	CHECK(port.RangeQueries == 2);
	CHECK(model.GetRange(kKey)->Ends.Tip == IdNumbered(3));
}

TEST_CASE("A range answered again unchanged keeps the pointer it handed out")
{
	FakeReviewPort port;
	port.Range = RangeWithTip(2);
	port.Range.Commits.resize(1);
	port.Range.Commits.front().Id = IdNumbered(2);
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	model.RequestRange(kKey);
	Settle(model, jobs);
	const Core::Ref<const ReviewRange> first = model.GetRange(kKey);

	model.InvalidateRanges();
	model.RequestRange(kKey);
	Settle(model, jobs);

	CHECK(port.RangeQueries == 2);
	CHECK(model.GetRange(kKey) == first);

	port.Range.Worktree = std::filesystem::path{"side-tree"};
	model.InvalidateRanges();
	model.RequestRange(kKey);
	Settle(model, jobs);

	REQUIRE(model.GetRange(kKey) != nullptr);
	CHECK(model.GetRange(kKey) != first);
	CHECK(model.GetRange(kKey)->Worktree == std::optional<std::filesystem::path>{"side-tree"});
}

TEST_CASE("An answer for a forgotten review is dropped even when the review is asked about again")
{
	FakeReviewPort port;
	port.RangeAnswers = {RangeWithTip(2), RangeWithTip(3)};
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};

	model.RequestRange(kKey);
	model.Forget(kKey);
	model.RequestRange(kKey);
	Settle(model, jobs);

	REQUIRE(model.GetRange(kKey) != nullptr);
	CHECK(model.GetRange(kKey)->Ends.Tip == IdNumbered(3));
}

TEST_CASE("A failed range keeps its error until a retry succeeds")
{
	FakeReviewPort port;
	port.RangeFailure = Core::Error{Core::ErrorCode::IoFailure, "disk on fire"};
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	model.RequestRange(kKey);
	Settle(model, jobs);

	REQUIRE(model.GetRangeError(kKey) != nullptr);
	CHECK(model.GetRangeError(kKey)->Message == "disk on fire");

	model.RequestRange(kKey);
	Settle(model, jobs);
	CHECK(port.RangeQueries == 1);

	port.RangeFailure.reset();
	port.Range = RangeWithTip(2);
	model.Retry(kKey);
	model.RequestRange(kKey);
	Settle(model, jobs);

	CHECK(model.GetRangeError(kKey) == nullptr);
	CHECK(model.GetRange(kKey) != nullptr);
}

TEST_CASE("A retried range is loading while its last error is still held")
{
	FakeReviewPort port;
	port.RangeFailure = Core::Error{Core::ErrorCode::IoFailure, "disk on fire"};
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	model.RequestRange(kKey);
	Settle(model, jobs);
	REQUIRE_FALSE(model.IsRangeLoading(kKey));

	model.Retry(kKey);
	model.RequestRange(kKey);

	CHECK(model.IsRangeLoading(kKey));
	CHECK(model.IsRangeRetrying(kKey));
	CHECK(model.GetRangeError(kKey) != nullptr);

	Settle(model, jobs);

	CHECK_FALSE(model.IsRangeRetrying(kKey));
}

TEST_CASE("A failed range read again in the background is loading but not retried, so its failure stays on screen")
{
	FakeReviewPort port;
	port.RangeFailure = Core::Error{Core::ErrorCode::IoFailure, "disk on fire"};
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	model.RequestRange(kKey);
	Settle(model, jobs);

	model.InvalidateRanges();
	model.RequestRange(kKey);

	CHECK(model.IsRangeLoading(kKey));
	CHECK_FALSE(model.IsRangeRetrying(kKey));
}

TEST_CASE("A retry asked while a background read of the range runs stays a retry until its own read lands")
{
	FakeReviewPort port;
	port.RangeFailure = Core::Error{Core::ErrorCode::IoFailure, "disk on fire"};
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	model.RequestRange(kKey);
	Settle(model, jobs);
	model.InvalidateRanges();
	model.RequestRange(kKey);

	model.Retry(kKey);
	Settle(model, jobs);

	CHECK(model.IsRangeRetrying(kKey));

	model.RequestRange(kKey);
	CHECK(model.IsRangeRetrying(kKey));

	Settle(model, jobs);

	CHECK_FALSE(model.IsRangeRetrying(kKey));
	CHECK(port.RangeQueries == 3);
}

TEST_CASE("A range read again after its branch moved is loading while the earlier range is still held")
{
	FakeReviewPort port;
	port.RangeAnswers = {RangeWithTip(2), RangeWithTip(3)};
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	model.RequestRange(kKey);
	Settle(model, jobs);

	model.InvalidateRanges();
	model.RequestRange(kKey);

	CHECK(model.IsRangeLoading(kKey));
	REQUIRE(model.GetRange(kKey) != nullptr);
	CHECK(model.GetRange(kKey)->Ends.Tip == IdNumbered(2));
}

TEST_CASE("A range's diff is fetched once for the same ends")
{
	FakeReviewPort port;
	port.RangeDiff.Files.push_back(TextFile("a.cpp"));
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const RangeEnds ends = RangeWithTip(2).Ends;
	const RangeDiffKey diffKey{.Ends = ends, .Whitespace = Core::WhitespaceInDiff::Shown};

	model.RequestCommittedDiff(diffKey);
	Settle(model, jobs);
	model.RequestCommittedDiff(diffKey);
	Settle(model, jobs);

	CHECK(port.RangeDiffQueries == 1);
	REQUIRE(model.GetCommittedDiff(diffKey) != nullptr);
	CHECK(model.GetCommittedDiff(diffKey)->Files.size() == 1);
}

TEST_CASE("A failed range diff is asked again only after a retry")
{
	FakeReviewPort port;
	port.Range = RangeWithTip(2);
	port.RangeDiffFailure = Core::Error{Core::ErrorCode::IoFailure, "no"};
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const RangeEnds ends = port.Range.Ends;
	const RangeDiffKey diffKey{.Ends = ends, .Whitespace = Core::WhitespaceInDiff::Shown};
	model.RequestRange(kKey);
	model.RequestCommittedDiff(diffKey);
	Settle(model, jobs);

	CHECK(model.IsCommittedDiffFailed(diffKey));
	CHECK(model.GetCommittedDiffError().has_value());

	model.RequestCommittedDiff(diffKey);
	Settle(model, jobs);
	CHECK(port.RangeDiffQueries == 1);

	port.RangeDiffFailure.reset();
	model.Retry(kKey);
	model.RequestCommittedDiff(diffKey);
	Settle(model, jobs);

	CHECK(port.RangeDiffQueries == 2);
	CHECK(model.GetCommittedDiff(diffKey) != nullptr);
	CHECK_FALSE(model.GetCommittedDiffError().has_value());
}

TEST_CASE("Committed texts are read once per file and never for a binary one")
{
	FakeReviewPort port;
	port.Texts.Old = "one\n";
	port.Texts.HasOld = true;
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const RangeEnds ends = RangeWithTip(2).Ends;
	const Core::FileDiff file = TextFile("a.cpp");
	Core::FileDiff binary = TextFile("image.png");
	binary.IsBinary = true;

	model.RequestCommittedTexts(ends, file);
	model.RequestCommittedTexts(ends, binary);
	Settle(model, jobs);
	model.RequestCommittedTexts(ends, file);
	Settle(model, jobs);

	CHECK(port.RangeTextReads == 1);
	REQUIRE(model.GetCommittedTexts(ends, file) != nullptr);
	CHECK(model.GetCommittedTexts(ends, file)->Old == "one\n");
	CHECK(model.GetCommittedTexts(ends, binary) == nullptr);
}

TEST_CASE("Failed committed texts are read again after a retry")
{
	FakeReviewPort port;
	port.Range = RangeWithTip(2);
	port.RangeTextsFailure = Core::Error{Core::ErrorCode::IoFailure, "locked"};
	port.Texts.HasNew = true;
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const RangeEnds ends = port.Range.Ends;
	const Core::FileDiff file = TextFile("a.cpp");
	model.RequestRange(kKey);
	model.RequestCommittedTexts(ends, file);
	Settle(model, jobs);
	model.RequestCommittedTexts(ends, file);
	Settle(model, jobs);

	CHECK(port.RangeTextReads == 1);
	CHECK(model.GetCommittedTexts(ends, file) == nullptr);

	port.RangeTextsFailure.reset();
	model.Retry(kKey);
	model.RequestCommittedTexts(ends, file);
	Settle(model, jobs);

	CHECK(port.RangeTextReads == 2);
	CHECK(model.GetCommittedTexts(ends, file) != nullptr);
}

TEST_CASE("Clearing drops every review and every held diff")
{
	FakeReviewPort port;
	port.Range = RangeWithTip(2);
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const RangeEnds ends = port.Range.Ends;
	const RangeDiffKey diffKey{.Ends = ends, .Whitespace = Core::WhitespaceInDiff::Shown};
	model.RequestRange(kKey);
	model.RequestCommittedDiff(diffKey);
	Settle(model, jobs);

	model.Clear();

	CHECK(model.GetRange(kKey) == nullptr);
	CHECK(model.GetCommittedDiff(diffKey) == nullptr);
}

TEST_CASE("A range's diff is held apart for each white space setting and the port is told which")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const RangeEnds ends = RangeWithTip(2).Ends;
	const Graphite::RangeDiffKey shown{.Ends = ends, .Whitespace = Core::WhitespaceInDiff::Shown};
	const Graphite::RangeDiffKey ignored{.Ends = ends, .Whitespace = Core::WhitespaceInDiff::Ignored};

	model.RequestCommittedDiff(shown);
	Settle(model, jobs);
	CHECK(port.LastRangeWhitespace == Core::WhitespaceInDiff::Shown);

	model.RequestCommittedDiff(ignored);
	Settle(model, jobs);
	CHECK(port.LastRangeWhitespace == Core::WhitespaceInDiff::Ignored);

	model.RequestCommittedDiff(shown);
	Settle(model, jobs);

	CHECK(port.RangeDiffQueries == 2);
	CHECK(model.GetCommittedDiff(shown) != nullptr);
	CHECK(model.GetCommittedDiff(ignored) != nullptr);
}
