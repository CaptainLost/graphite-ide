#pragma once

#include "Core/Base/Base.h"

#include "Review/ReviewFiles.h"
#include "Review/ReviewSessions.h"

#include "Core/Git/Ref.h"

#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// The base a branch's review is drawn against: its session's or its draft's, else the one OfferReview offers, else
	// the trunk. Empty with none of them, and when the one left is the branch itself.
	std::optional<std::string> ReviewBaseOf(const std::string& branch, const std::vector<Core::Branch>& branches,
											const ReviewSessions& sessions);

	// Why ReviewBaseOf has no base for a branch: the trunk has nothing to be reviewed against, or there is no trunk.
	std::string NoReviewBaseMessage(const std::string& branch, const std::vector<Core::Branch>& branches);
}
