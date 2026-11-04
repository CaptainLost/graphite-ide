#include "Review/ReviewProgress.h"

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

	std::string NoReviewBaseMessage(const std::string& branch, const std::vector<Core::Branch>& branches,
									const BranchLines* lines)
	{
		const Core::Branch* named = BranchWithFullName(branches, branch);
		const std::string shortName = named != nullptr ? named->Name : Core::ShortBranchName(branch);

		if (lines != nullptr && lines->BaseNamed(branch) != nullptr)
		{
			return "There is nothing to review " + shortName + " against.";
		}

		return shortName + " is compared with nothing, so there is nothing to review it against. "
						   "Pick a line with Compare with\xE2\x80\xA6 or use Review against.";
	}
}
