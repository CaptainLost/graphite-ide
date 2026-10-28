#include "Review/ReviewMenu.h"

#include "Review/ReviewFiles.h"

#include <optional>

namespace Graphite {

	namespace {

		constexpr const char* kReview = "Review";
		constexpr const char* kOpenReview = "Open review";
		constexpr const char* kEndReview = "End review\xE2\x80\xA6";
		constexpr const char* kReviewAgainst = "Review against";

		ReviewMenu SessionMenu(const std::string& branch)
		{
			ReviewMenu menu;
			menu.Items.push_back(ReviewMenuItem{.Label = kOpenReview, .Action = ReviewAction::Open, .Branch = branch});
			menu.Items.push_back(ReviewMenuItem{.Label = kEndReview, .Action = ReviewAction::End, .Branch = branch});
			return menu;
		}

		void AddBasePicker(ReviewMenu& menu, const Core::Branch& reviewed, const std::vector<Core::Branch>& branches)
		{
			for (const Core::Branch& candidate : branches)
			{
				const bool isAnotherLocalBranch =
					candidate.Kind == Core::RefKind::Local && candidate.FullName != reviewed.FullName;

				if (isAnotherLocalBranch)
				{
					menu.SubmenuItems.push_back(ReviewMenuItem{.Label = candidate.Name,
															   .Action = ReviewAction::Start,
															   .Branch = reviewed.FullName,
															   .Base = candidate.FullName});
				}
			}

			if (!menu.SubmenuItems.empty())
			{
				menu.Submenu = kReviewAgainst;
			}
		}
	}

	ReviewMenu ReviewMenuFor(const Core::Branch& branch, const std::vector<Core::Branch>& branches,
							 const ReviewSessions& sessions)
	{
		if (sessions.IsKept(branch.FullName))
		{
			return SessionMenu(branch.FullName);
		}

		ReviewMenu menu;
		const std::optional<ReviewOffer> offer = OfferReview(branch, branches);

		// The thread it opens drafts its own base (ReviewBaseOf); the offer only says there is one.
		if (offer.has_value() && offer->Base.has_value())
		{
			menu.Items.push_back(ReviewMenuItem{.Label = kReview, .Action = ReviewAction::Open, .Branch = offer->Branch});
		}

		AddBasePicker(menu, branch, branches);
		return menu;
	}
}
