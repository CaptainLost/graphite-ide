#include "Git/History/ForkSeparator.h"

#include "Review/ReviewFiles.h"

#include "Core/Git/Oid.h"

#include <unordered_set>

namespace Graphite {

	namespace {

		// WARNING: Bounds the own commits looked up, not the commits shown. A branch with more than
		// this gets no separator, because the list would put it in the wrong place.
		constexpr std::size_t kMostOwnCommitsLooked = 5000;

		const Core::Branch* BranchShown(const std::optional<std::string>& selectedRef,
										const std::vector<Core::Branch>& branches)
		{
			for (const Core::Branch& branch : branches)
			{
				const bool isShown = selectedRef.has_value()
										 ? branch.FullName == *selectedRef
										 : branch.Kind == Core::RefKind::Local && branch.IsHead;

				if (isShown)
				{
					return &branch;
				}
			}

			return nullptr;
		}

		const Core::Branch* BranchNamed(const std::vector<Core::Branch>& branches, const std::string& fullName)
		{
			for (const Core::Branch& branch : branches)
			{
				if (branch.FullName == fullName)
				{
					return &branch;
				}
			}

			return nullptr;
		}
	}

	std::optional<ForkBase> FindForkBase(const std::optional<std::string>& selectedRef,
										 const std::vector<Core::Branch>& branches)
	{
		const Core::Branch* branch = BranchShown(selectedRef, branches);

		if (branch == nullptr)
		{
			return std::nullopt;
		}

		const std::optional<ReviewOffer> offer = OfferReview(*branch, branches);

		if (!offer.has_value() || !offer->Base.has_value())
		{
			return std::nullopt;
		}

		const Core::Branch* base = BranchNamed(branches, *offer->Base);

		if (base == nullptr)
		{
			return std::nullopt;
		}

		// WARNING: A remote branch against the local one that tracks it is one line of history, not a
		// branch and where it began.
		const bool isUpstreamOfBase = base->Upstream == branch->Name;

		if (isUpstreamOfBase)
		{
			return std::nullopt;
		}

		ForkBase fork;
		fork.OwnCommits.Ref = branch->FullName;
		fork.OwnCommits.Exclude = base->Tip;
		fork.OwnCommits.MaxCount = kMostOwnCommitsLooked;
		fork.BaseName = Core::ShortBranchName(base->FullName);

		return fork;
	}

	std::optional<ForkSeparator> FindForkSeparator(const Core::LogPage& page, const Core::LogPage& ownCommits,
												   const std::string& baseName)
	{
		if (!ownCommits.Complete || ownCommits.Commits.empty())
		{
			return std::nullopt;
		}

		std::unordered_set<Core::Oid> own;

		for (const Core::CommitSummary& commit : ownCommits.Commits)
		{
			own.insert(commit.Id);
		}

		for (std::size_t commitIndex = 0; commitIndex < page.Commits.size(); ++commitIndex)
		{
			if (own.contains(page.Commits[commitIndex].Id))
			{
				continue;
			}

			return ForkSeparator{.BeforeCommit = commitIndex,
								 .Text = "Earlier history \xC2\xB7 already in " + baseName};
		}

		return std::nullopt;
	}
}
