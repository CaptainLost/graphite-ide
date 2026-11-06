#include "corepch.h"

#include "Core/Git/Content/ContentCheck.h"

#include "Core/Git/Content/ContentAnswer.h"
#include "Core/Git/Libgit2Support.h"
#include "Core/Git/Repository.h"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace Core {

	namespace {

		ContentVerdict VerdictOf(ContentOutcome outcome)
		{
			return ContentVerdict{.Outcome = outcome};
		}

		bool EveryOwnChangeHasTwin(const PatchMatch& match)
		{
			return !match.MissingObjects && match.OwnChanges > 0 && match.InBase == match.OwnChanges;
		}

		bool PatchWalkStoppedShort(const PatchMatch& match)
		{
			return match.MissingObjects || !match.Complete;
		}

		// The merge already said the line holds the branch's changes, so a landing that cannot be found
		// for any reason leaves a squash without one rather than failing the check.
		std::optional<Oid> FindLanding(const Repository& repository, const Oid& mergeBase, const Oid& branchTip,
									   const Oid& lineTip, const ContentBudgets& budgets)
		{
			const std::optional<Oid> noLanding;
			const Result<std::vector<Oid>> candidates = repository.LineCommitsTouchingBranch(
				lineTip, mergeBase, branchTip, budgets.LandingChain, budgets.LandingTries);

			if (!candidates.HasValue())
			{
				return noLanding;
			}

			for (const Oid& candidate : candidates.GetValue())
			{
				const Result<ContentAnswer> answer = repository.MergeChangesNothing(candidate, branchTip);

				if (!answer.HasValue())
				{
					return noLanding;
				}

				if (answer.GetValue() == ContentAnswer::Yes)
				{
					return std::optional<Oid>{candidate};
				}

				// WARNING: A candidate that cannot be judged may itself be the landing, so naming a later one
				// could name a commit that is not the oldest.
				if (answer.GetValue() == ContentAnswer::Unknown)
				{
					return noLanding;
				}
			}

			return noLanding;
		}

		ContentVerdict SquashVerdict(const Repository& repository, const Oid& mergeBase, const Oid& branchTip,
									 const Oid& lineTip, const std::vector<PatchTwin>& twins, const ContentBudgets& budgets)
		{
			return ContentVerdict{
				.Outcome = ContentOutcome::Merged,
				.Kind = ContentKind::Squash,
				.Landing = FindLanding(repository, mergeBase, branchTip, lineTip, budgets),
				.Twins = twins,
			};
		}

		Result<bool> HoldsBothTips(const Repository& repository, const Oid& branchTip, const Oid& lineTip)
		{
			const Result<bool> holdsBranch = repository.HoldsCommit(branchTip);

			if (!holdsBranch.HasValue() || !holdsBranch.GetValue())
			{
				return holdsBranch;
			}

			return repository.HoldsCommit(lineTip);
		}

		Result<ContentVerdict> CheckWithObjectsInMemory(const Repository& repository, const Oid& branchTip,
														const Oid& lineTip, PatchIdCache& cache, const ContentBudgets& budgets)
		{
			const Result<bool> holdsBothTips = HoldsBothTips(repository, branchTip, lineTip);

			if (!holdsBothTips.HasValue())
			{
				return holdsBothTips.GetError();
			}

			if (!holdsBothTips.GetValue())
			{
				return VerdictOf(ContentOutcome::Unknown);
			}

			Result<Oid> mergeBase = repository.MergeBase(branchTip, lineTip);

			if (!mergeBase.HasValue())
			{
				const bool noCommonHistory = mergeBase.GetError().Code == ErrorCode::NotFound;

				if (noCommonHistory)
				{
					return VerdictOf(ContentOutcome::NotMerged);
				}

				return std::move(mergeBase).GetError();
			}

			const Result<bool> changesCancelOut = repository.HaveSameTree(mergeBase.GetValue(), branchTip);

			if (!changesCancelOut.HasValue())
			{
				return changesCancelOut.GetError();
			}

			if (changesCancelOut.GetValue())
			{
				return VerdictOf(ContentOutcome::NotMerged);
			}

			const Result<ContentAnswer> mergeChangesNothing = repository.MergeChangesNothing(lineTip, branchTip);

			if (!mergeChangesNothing.HasValue())
			{
				return mergeChangesNothing.GetError();
			}

			const bool lineHoldsTheChanges = mergeChangesNothing.GetValue() == ContentAnswer::Yes;
			Result<PatchMatch> patches = repository.MatchPatches(branchTip, lineTip, budgets.LinePatches, cache);

			if (!patches.HasValue())
			{
				if (!IsMissingObject(patches.GetError()))
				{
					return std::move(patches).GetError();
				}

				if (lineHoldsTheChanges)
				{
					const std::vector<PatchTwin> noTwins;
					return SquashVerdict(repository, mergeBase.GetValue(), branchTip, lineTip, noTwins, budgets);
				}

				return VerdictOf(ContentOutcome::Unknown);
			}

			const PatchMatch& match = patches.GetValue();

			if (EveryOwnChangeHasTwin(match))
			{
				return ContentVerdict{.Outcome = ContentOutcome::Merged, .Kind = ContentKind::Rebase, .Twins = match.Twins};
			}

			if (lineHoldsTheChanges)
			{
				return SquashVerdict(repository, mergeBase.GetValue(), branchTip, lineTip, match.Twins, budgets);
			}

			if (mergeChangesNothing.GetValue() == ContentAnswer::Unknown || PatchWalkStoppedShort(match))
			{
				return VerdictOf(ContentOutcome::Unknown);
			}

			return VerdictOf(ContentOutcome::NotMerged);
		}
	}

	Result<ContentVerdict> CheckMergedByContent(const Repository& repository, const Oid& branchTip, const Oid& lineTip,
												PatchIdCache& cache)
	{
		return CheckMergedByContent(repository, branchTip, lineTip, cache, ContentBudgets{});
	}

	Result<ContentVerdict> CheckMergedByContent(const Repository& repository, const Oid& branchTip, const Oid& lineTip,
												PatchIdCache& cache, const ContentBudgets& budgets)
	{
		Result<ContentVerdict> verdict = CheckWithObjectsInMemory(repository, branchTip, lineTip, cache, budgets);
		repository.DropObjectsInMemory();
		return verdict;
	}
}
