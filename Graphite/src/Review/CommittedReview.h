#pragma once

#include "Review/ReviewModel.h"

#include "Core/Base/Base.h"
#include "Core/Git/Diff.h"

#include <cstdint>
#include <string>

namespace Graphite {

	enum class CommittedReviewState : std::uint8_t
	{
		LoadingRange,
		RangeFailed,
		RangeProblem,
		LoadingChanges,
		ChangesFailed,
		Ready
	};

	// The committed half of a branch's review as the model holds it now, read the same way by the branch's card and
	// by its review tab.
	struct CommittedReview
	{
		CommittedReviewState State = CommittedReviewState::LoadingRange;

		Core::Ref<const ReviewRange> Range;
		Core::Ref<const Core::Diff> Committed;

		// What to say for RangeFailed, RangeProblem and ChangesFailed.
		std::string Problem;
	};

	// Asks for the range, and for its changes once the range has commits and no problem.
	void AskForCommittedReview(ReviewModel& model, const ReviewKey& key, Core::WhitespaceInDiff whitespace);

	// Asks for nothing.
	CommittedReview ReadCommittedReview(const ReviewModel& model, const ReviewKey& key, Core::WhitespaceInDiff whitespace);
}
