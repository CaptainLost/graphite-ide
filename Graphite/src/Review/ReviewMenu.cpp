#include "Review/ReviewMenu.h"

#include <IconsLucide.h>

#include <format>
#include <optional>

namespace Graphite {

	namespace {

		constexpr const char* kReview = "Review";
		constexpr const char* kOpenReview = "Open review";
		constexpr const char* kEndReview = "End review\xE2\x80\xA6";
		constexpr const char* kReviewAgainst = "Review against";
		constexpr const char* kProblemMark = " ?";
		constexpr const char* kMerged = " merged";

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

		ViewedCount ViewedInCommits(const ReviewSessions& sessions, const std::string& branch, const Core::Diff& committed)
		{
			const ReviewGroups groups{.Commits = &committed};
			const ViewedStateOf stateOf = [&sessions, &branch](ReviewGroup group, const Core::FileDiff& file) {
				return sessions.StateOf(branch, group, file);
			};

			return CountViewed(groups, stateOf);
		}

		bool BadgeCountsDiff(const ReviewRange& range)
		{
			return range.Problem == RangeProblem::None && range.Ends.MergeBase != range.Ends.Tip;
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

		if (offer.has_value() && offer->Base.has_value())
		{
			menu.Items.push_back(ReviewMenuItem{.Label = kReview,
												.Action = ReviewAction::Open,
												.Branch = offer->Branch,
												.Base = *offer->Base});
		}

		AddBasePicker(menu, branch, branches);
		return menu;
	}

	std::string ReviewBadgeText(const ReviewSessions& sessions, const std::string& branch, const ReviewRange* range,
								const Core::Error* rangeError, const Core::Diff* committed)
	{
		if (!sessions.IsKept(branch))
		{
			return std::string{};
		}

		const std::string mark = ICON_LC_CIRCLE_DOT;

		if (range == nullptr)
		{
			return rangeError != nullptr ? mark + kProblemMark : mark;
		}

		if (range->Problem != RangeProblem::None)
		{
			return mark + kProblemMark;
		}

		if (range->Ends.MergeBase == range->Ends.Tip)
		{
			return std::string{ICON_LC_CHECK} + kMerged;
		}

		if (committed == nullptr)
		{
			return mark;
		}

		const ViewedCount viewed = ViewedInCommits(sessions, branch, *committed);
		return std::format("{} {}/{}", mark, viewed.Viewed, viewed.Total);
	}

	std::string ReviewBadges::TextFor(const ReviewSessions& sessions, const std::string& branch,
									  const Core::Ref<const ReviewRange>& range, const Core::Error* rangeError,
									  const Core::Ref<const Core::Diff>& committed)
	{
		HeldBadge& held = m_Badges[branch];
		const bool rangeFailed = rangeError != nullptr;
		const bool isCurrent = !held.Text.empty() && held.Range == range && held.RangeFailed == rangeFailed &&
							   held.Committed == committed && held.SessionsVersion == sessions.GetVersion();

		if (!isCurrent)
		{
			held = HeldBadge{.Range = range,
							 .RangeFailed = rangeFailed,
							 .Committed = committed,
							 .SessionsVersion = sessions.GetVersion(),
							 .Text = ReviewBadgeText(sessions, branch, range.get(), rangeError, committed.get())};
		}

		return held.Text;
	}

	std::optional<std::string> ReviewBadges::HeldFor(const ReviewSessions& sessions, const std::string& branch,
													 const Core::Ref<const ReviewRange>& range) const
	{
		if (range == nullptr || !m_Badges.contains(branch))
		{
			return std::nullopt;
		}

		const HeldBadge& held = m_Badges.at(branch);
		const bool workedOutForThis =
			!held.Text.empty() && held.Range == range && held.SessionsVersion == sessions.GetVersion();
		const bool waitsForDiff = BadgeCountsDiff(*range) && held.Committed == nullptr;

		if (!workedOutForThis || waitsForDiff)
		{
			return std::nullopt;
		}

		return held.Text;
	}
}
