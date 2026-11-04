#include "Review/ReviewProgress.h"

#include "Git/DefaultBranch.h"

namespace Graphite {

	std::optional<std::string> ReviewBaseOf(const std::string& branch, const std::vector<Core::Branch>& branches,
											const ReviewSessions& sessions)
	{
		if (const ReviewSession* session = sessions.Find(branch); session != nullptr)
		{
			return session->Base;
		}

		if (const Core::Branch* reviewed = BranchWithFullName(branches, branch); reviewed != nullptr)
		{
			if (const std::optional<ReviewOffer> offer = OfferReview(*reviewed, branches);
				offer && offer->Base && *offer->Base != branch)
			{
				return offer->Base;
			}
		}

		// WARNING: The trunk would otherwise be drafted against itself, an empty review that its tab would keep in
		// reviews.json.
		const Core::Branch* trunk = TrunkBranch(branches);
		return trunk != nullptr && trunk->FullName != branch ? std::optional<std::string>{trunk->FullName} : std::nullopt;
	}

	std::string NoReviewBaseMessage(const std::string& branch, const std::vector<Core::Branch>& branches)
	{
		if (const Core::Branch* trunk = TrunkBranch(branches); trunk != nullptr && trunk->FullName == branch)
		{
			return "There is nothing to review " + trunk->Name + " against.";
		}

		return "There is no trunk to review this branch against.";
	}
}
