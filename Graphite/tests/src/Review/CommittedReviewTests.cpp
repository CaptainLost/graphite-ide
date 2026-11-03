#include <doctest/doctest.h>

#include "Review/CommittedReview.h"
#include "Review/ReviewFiles.h"
#include "Review/ReviewModel.h"

#include "DeferredJobs.h"
#include "Review/FakeReviewPort.h"

#include "Core/Base/Timestep.h"

#include <cstdint>
#include <string>

using Graphite::CommittedReview;
using Graphite::CommittedReviewState;
using Graphite::ReviewKey;
using Graphite::ReviewModel;

namespace {

	const ReviewKey kKey{.Branch = "refs/heads/side", .Base = "refs/heads/main"};
	constexpr Core::WhitespaceInDiff kShown = Core::WhitespaceInDiff::Shown;

	Core::Oid IdNumbered(std::uint8_t number)
	{
		Core::Oid id;
		id.Bytes[0] = number;
		return id;
	}

	Graphite::ReviewRange RangeFrom(std::uint8_t mergeBase, std::uint8_t tip)
	{
		Graphite::ReviewRange range;
		range.Ends = Graphite::RangeEnds{.MergeBase = IdNumbered(mergeBase), .Tip = IdNumbered(tip)};
		return range;
	}

	Core::FileDiff Changed(const char* path)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.OldPath = path;
		file.NewPath = path;
		return file;
	}

	// Asks as a card or a tab does every frame, lets the jobs run, and reads what the model holds after.
	CommittedReview AskAndSettle(ReviewModel& model, DeferredJobs& jobs)
	{
		Graphite::AskForCommittedReview(model, kKey, kShown);
		jobs.RunAll();
		model.OnUpdate(Core::Timestep{0.0f});

		return Graphite::ReadCommittedReview(model, kKey, kShown);
	}
}

TEST_CASE("A review waits for its range, then for its changes, then is ready")
{
	FakeReviewPort port;
	port.Range = RangeFrom(1, 2);
	port.RangeDiff.Files.push_back(Changed("a.cpp"));
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};

	CHECK(Graphite::ReadCommittedReview(model, kKey, kShown).State == CommittedReviewState::LoadingRange);

	const CommittedReview withRange = AskAndSettle(model, jobs);
	CHECK(withRange.State == CommittedReviewState::LoadingChanges);
	REQUIRE(withRange.Range != nullptr);

	const CommittedReview ready = AskAndSettle(model, jobs);
	CHECK(ready.State == CommittedReviewState::Ready);
	REQUIRE(ready.Committed != nullptr);
	CHECK(ready.Committed->Files.size() == 1);
}

TEST_CASE("A range that could not be read is a failure with its reason, and loading again while retried")
{
	FakeReviewPort port;
	port.RangeFailure = Core::Error{Core::ErrorCode::IoFailure, "the repository is locked"};
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};

	const CommittedReview failed = AskAndSettle(model, jobs);
	CHECK(failed.State == CommittedReviewState::RangeFailed);
	CHECK(failed.Problem == "the repository is locked");

	model.Retry(kKey);

	CHECK(Graphite::ReadCommittedReview(model, kKey, kShown).State == CommittedReviewState::LoadingRange);
}

TEST_CASE("A range with a problem says what it is for the branch and its base")
{
	FakeReviewPort port;
	port.Range = RangeFrom(1, 2);
	port.Range.Problem = Graphite::RangeProblem::BaseMissing;
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};

	const CommittedReview review = AskAndSettle(model, jobs);

	CHECK(review.State == CommittedReviewState::RangeProblem);
	CHECK(review.Problem == Graphite::RangeProblemMessage(Graphite::RangeProblem::BaseMissing, kKey.Branch, kKey.Base));
	CHECK(port.RangeDiffQueries == 0);
}

TEST_CASE("A range with no commits is ready with no changes, without asking for them")
{
	FakeReviewPort port;
	port.Range = RangeFrom(1, 1);
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};

	AskAndSettle(model, jobs);
	const CommittedReview review = AskAndSettle(model, jobs);

	CHECK(review.State == CommittedReviewState::Ready);
	REQUIRE(review.Committed != nullptr);
	CHECK(review.Committed->Files.empty());
	CHECK(port.RangeDiffQueries == 0);
}

TEST_CASE("Changes that could not be read are a failure with their own reason")
{
	FakeReviewPort port;
	port.Range = RangeFrom(1, 2);
	port.RangeDiffFailure = Core::Error{Core::ErrorCode::IoFailure, "a pack is missing"};
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};

	AskAndSettle(model, jobs);
	const CommittedReview review = AskAndSettle(model, jobs);

	CHECK(review.State == CommittedReviewState::ChangesFailed);
	CHECK(review.Problem == "a pack is missing");
}

TEST_CASE("Reading a review asks the model for nothing")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};

	static_cast<void>(Graphite::ReadCommittedReview(model, kKey, kShown));

	CHECK(jobs.Count() == 0);
}
