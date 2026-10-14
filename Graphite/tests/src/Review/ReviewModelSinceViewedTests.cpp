#include <doctest/doctest.h>

#include "Review/ReviewModel.h"

#include "DeferredJobs.h"
#include "Review/FakeReviewPort.h"

using Graphite::ReviewModel;
using Graphite::SinceViewedRequest;

namespace {

	const Core::Timestep kFrame{0.016f};

	const Graphite::ReviewKey kAnyReview{.Branch = "refs/heads/side", .Base = "refs/heads/main"};

	Core::Oid IdNumbered(std::uint8_t number)
	{
		Core::Oid id;
		id.Bytes[0] = number;
		return id;
	}

	SinceViewedRequest RequestFor(std::uint8_t viewed, std::uint8_t current, Core::WhitespaceInDiff whitespace)
	{
		return SinceViewedRequest{.ViewedBlob = IdNumbered(viewed),
								  .CurrentBlob = IdNumbered(current),
								  .Path = "a.cpp",
								  .Whitespace = whitespace,
								  .Tip = IdNumbered(50),
								  .Worktree = std::nullopt};
	}

	void Settle(ReviewModel& model, DeferredJobs& jobs)
	{
		jobs.RunAll();
		model.OnUpdate(kFrame);
	}
}

TEST_CASE("The diff since the viewed version is read once and held with its texts")
{
	FakeReviewPort port;
	port.SinceViewed.Texts.Old = "viewed\n";
	port.SinceViewed.Texts.HasOld = true;
	port.SinceViewed.Texts.New = "now\n";
	port.SinceViewed.Texts.HasNew = true;
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const SinceViewedRequest request = RequestFor(1, 2, Core::WhitespaceInDiff::Shown);

	model.RequestSinceViewed(request);
	Settle(model, jobs);
	model.RequestSinceViewed(request);
	Settle(model, jobs);

	CHECK(port.SinceViewedReads == 1);
	REQUIRE(model.GetSinceViewed(request) != nullptr);
	CHECK(model.GetSinceViewed(request)->Texts.Old == "viewed\n");
	CHECK_FALSE(model.IsSinceViewedUnavailable(request));
	REQUIRE(port.LastSinceViewed.has_value());
	CHECK(port.LastSinceViewed->Tip == std::optional<Core::Oid>{IdNumbered(50)});
}

TEST_CASE("A viewed version missing from the repository reads as unavailable")
{
	FakeReviewPort port;
	port.SinceViewedFailure = Core::Error{Core::ErrorCode::NotFound, "no blob"};
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const SinceViewedRequest request = RequestFor(1, 2, Core::WhitespaceInDiff::Shown);

	model.RequestSinceViewed(request);
	Settle(model, jobs);

	CHECK(model.GetSinceViewed(request) == nullptr);
	CHECK(model.IsSinceViewedUnavailable(request));
	CHECK(model.IsSinceViewedFailed(request));

	port.SinceViewedFailure.reset();
	model.Retry(kAnyReview);
	model.RequestSinceViewed(request);
	Settle(model, jobs);

	CHECK(port.SinceViewedReads == 2);
	CHECK_FALSE(model.IsSinceViewedUnavailable(request));
	CHECK_FALSE(model.IsSinceViewedFailed(request));
	CHECK(model.GetSinceViewed(request) != nullptr);
}

TEST_CASE("A since-viewed read that failed for another reason is not read as a viewed version gone")
{
	FakeReviewPort port;
	port.SinceViewedFailure = Core::Error{Core::ErrorCode::TooLarge, "the file on disk is too large"};
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const SinceViewedRequest request = RequestFor(1, 2, Core::WhitespaceInDiff::Shown);

	model.RequestSinceViewed(request);
	Settle(model, jobs);

	CHECK(model.GetSinceViewed(request) == nullptr);
	CHECK_FALSE(model.IsSinceViewedUnavailable(request));
	CHECK(model.IsSinceViewedFailed(request));
}

TEST_CASE("The since-viewed answer is held apart for each white space setting")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};

	model.RequestSinceViewed(RequestFor(1, 2, Core::WhitespaceInDiff::Shown));
	model.RequestSinceViewed(RequestFor(1, 2, Core::WhitespaceInDiff::Ignored));
	Settle(model, jobs);

	CHECK(port.SinceViewedReads == 2);
}
