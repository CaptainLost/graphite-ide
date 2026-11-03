#include "Review/CommittedReview.h"

#include "Review/ReviewFiles.h"

#include <optional>

namespace Graphite {

	namespace {
		constexpr const char* kChangesUnreadable = "The branch's changes could not be read.";

		bool HasCommits(const ReviewRange& range)
		{
			return range.Ends.MergeBase != range.Ends.Tip;
		}

		RangeDiffKey ChangesKeyOf(const ReviewRange& range, Core::WhitespaceInDiff whitespace)
		{
			return RangeDiffKey{.Ends = range.Ends, .Whitespace = whitespace};
		}

		// WARNING: One empty diff shared by every range without commits: there is nothing in it to read, and asking git
		// for it would take one of the model's few held diffs.
		const Core::Ref<const Core::Diff>& NoChanges()
		{
			static const Core::Ref<const Core::Diff> empty = Core::CreateRef<const Core::Diff>();
			return empty;
		}
	}

	void AskForCommittedReview(ReviewModel& model, const ReviewKey& key, Core::WhitespaceInDiff whitespace)
	{
		model.RequestRange(key);

		const Core::Ref<const ReviewRange> range = model.GetRange(key);

		if (range == nullptr || range->Problem != RangeProblem::None || !HasCommits(*range))
		{
			return;
		}

		model.RequestCommittedDiff(ChangesKeyOf(*range, whitespace));
	}

	CommittedReview ReadCommittedReview(const ReviewModel& model, const ReviewKey& key, Core::WhitespaceInDiff whitespace)
	{
		CommittedReview review;
		review.Range = model.GetRange(key);

		if (review.Range == nullptr)
		{
			const Core::Error* error = model.GetRangeError(key);

			// WARNING: A retry the reader asked for is waited on, not shown as the failure it is retrying.
			if (error != nullptr && !model.IsRangeRetrying(key))
			{
				review.State = CommittedReviewState::RangeFailed;
				review.Problem = error->Message;
			}

			return review;
		}

		if (review.Range->Problem != RangeProblem::None)
		{
			review.State = CommittedReviewState::RangeProblem;
			review.Problem = RangeProblemMessage(review.Range->Problem, key.Branch, key.Base);
			return review;
		}

		if (!HasCommits(*review.Range))
		{
			review.State = CommittedReviewState::Ready;
			review.Committed = NoChanges();
			return review;
		}

		const RangeDiffKey changesKey = ChangesKeyOf(*review.Range, whitespace);
		review.Committed = model.GetCommittedDiff(changesKey);

		if (review.Committed != nullptr)
		{
			review.State = CommittedReviewState::Ready;
			return review;
		}

		if (!model.IsCommittedDiffFailed(changesKey))
		{
			review.State = CommittedReviewState::LoadingChanges;
			return review;
		}

		const std::optional<Core::Error> failure = model.GetCommittedDiffFailure(changesKey);
		review.State = CommittedReviewState::ChangesFailed;
		review.Problem = failure.has_value() ? failure->Message : std::string{kChangesUnreadable};

		return review;
	}
}
