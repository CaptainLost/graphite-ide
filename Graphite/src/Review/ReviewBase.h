#pragma once

#include "Core/Base/Base.h"

#include "Review/ReviewFiles.h"
#include "Review/ReviewSessions.h"

#include "Core/Git/Ref.h"

#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// The base a branch's review is drawn against: its session's or its draft's, else the one OfferReview offers. Empty
	// with neither, and when the offer is the branch itself.
	std::optional<std::string> ReviewBaseOf(const std::string& branch, const std::vector<Core::Branch>& branches,
											const ReviewSessions& sessions, const BranchLines* lines);
}
