#pragma once

#include "Core/Base/Base.h"

#include "Review/ReviewFiles.h"
#include "Review/ReviewModel.h"
#include "Review/ReviewPort.h"
#include "Review/ReviewSessions.h"

#include "Core/Git/Diff.h"
#include "Core/Git/Ref.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Graphite {

	// The base a branch's review is drawn and counted against: its session's or its draft's, else the one
	// OfferReview offers, else the trunk. Empty with none of them.
	std::optional<std::string> ReviewBaseOf(const std::string& branch, const std::vector<Core::Branch>& branches,
											const ReviewSessions& sessions);

	// Files viewed out of the files in the committed range. A kept session counts its viewed marks; a draft or no
	// session has viewed none. Empty while the range or its diff is unknown, and for a range with a problem.
	std::optional<ViewedCount> CountReviewProgress(const ReviewSessions& sessions, const std::string& branch,
												   const ReviewRange* range, const Core::Diff* committed);

	// WARNING: Counting a large range's viewed files on every frame would cost the frame, and asking for a diff on
	// every frame keeps it among the model's few held diffs, so the counts of many cards would push each other's
	// diffs out and have them read again. A count is held by branch until its range, its marks or its whitespace
	// setting change. Clear it when the open folder changes: everything held is about the folder that was open.
	class ReviewProgress
	{
	public:
		// Asks `model` for the range and the diff the count needs.
		std::optional<ViewedCount> CountOf(const std::string& branch,
										   const Core::Ref<const std::vector<Core::Branch>>& branches,
										   const ReviewSessions& sessions, ReviewModel& model);

		void Clear();

	private:
		struct HeldCount
		{
			Core::Ref<const ReviewRange> Range;
			Core::WhitespaceInDiff Whitespace = Core::WhitespaceInDiff::Shown;
			bool Kept = false;
			std::uint64_t SessionsVersion = 0;
			ViewedCount Count;
		};

		std::optional<std::string> BaseOf(const std::string& branch,
										  const Core::Ref<const std::vector<Core::Branch>>& branches,
										  const ReviewSessions& sessions);

		// WARNING: The snapshot is held, not only compared, so one freed and another made at its address can never
		// pass for the one the bases were worked out from.
		Core::Ref<const std::vector<Core::Branch>> m_BasesFrom;
		std::unordered_map<std::string, std::optional<std::string>> m_Bases;

		std::unordered_map<std::string, HeldCount> m_Counts;
	};
}
