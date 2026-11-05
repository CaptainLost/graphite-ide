#include "Review/ReviewBase.h"

#include "Git/Branches/BranchLookup.h"

namespace Graphite {

	std::optional<std::string> ReviewBaseOf(const std::string& branch, const std::vector<Core::Branch>& branches,
											const ReviewSessions& sessions, const BranchLines* lines)
	{
		if (const ReviewSession* session = sessions.Find(branch); session != nullptr)
		{
			return session->Base;
		}

		if (const Core::Branch* reviewed = BranchWithFullName(branches, branch); reviewed != nullptr)
		{
			if (const std::optional<ReviewOffer> offer = OfferReview(*reviewed, branches, lines);
				offer && offer->Base && *offer->Base != branch)
			{
				return offer->Base;
			}
		}

		return std::nullopt;
	}
}
