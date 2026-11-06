#include "corepch.h"

#include "Core/Git/Content/ContentCheck.h"

#include "Core/Git/Content/ContentAnswer.h"
#include "Core/Git/Content/ContentThresholds.h"
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

		bool IsMissing(const Error& failure)
		{
			return failure.Code == ErrorCode::NotFound;
		}

		Result<std::optional<Oid>> FindLanding(const Repository& repository, const Oid& mergeBase, const Oid& branchTip,
											   const Oid& lineTip)
		{
			const std::optional<Oid> noLanding;
			Result<std::vector<Oid>> candidates = repository.LineCommitsTouchingBranch(lineTip, mergeBase, branchTip);

			if (!candidates.HasValue())
			{
				if (IsMissing(candidates.GetError()))
				{
					return noLanding;
				}

				return std::move(candidates).GetError();
			}

			std::size_t tries = 0;

			for (const Oid& candidate : candidates.GetValue())
			{
				if (tries == kLandingTries)
				{
					break;
				}

				++tries;

				const Result<ContentAnswer> answer = repository.MergeChangesNothing(candidate, branchTip);

				if (!answer.HasValue())
				{
					return answer.GetError();
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

		Result<ContentVerdict> SquashVerdict(const Repository& repository, const Oid& mergeBase, const Oid& branchTip,
											 const Oid& lineTip, const PatchMatch& match)
		{
			Result<std::optional<Oid>> landing = FindLanding(repository, mergeBase, branchTip, lineTip);

			if (!landing.HasValue())
			{
				return std::move(landing).GetError();
			}

			return ContentVerdict{
				.Outcome = ContentOutcome::Merged,
				.Kind = ContentKind::Squash,
				.Landing = landing.GetValue(),
				.Twins = match.Twins,
			};
		}

		Result<ContentVerdict> CheckWithObjectsInMemory(const Repository& repository, const Oid& branchTip,
														const Oid& lineTip, PatchIdCache& cache)
		{
			if (!repository.HoldsCommit(branchTip) || !repository.HoldsCommit(lineTip))
			{
				return VerdictOf(ContentOutcome::Unknown);
			}

			Result<Oid> mergeBase = repository.MergeBase(branchTip, lineTip);

			if (!mergeBase.HasValue())
			{
				if (IsMissing(mergeBase.GetError()))
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

			Result<PatchMatch> patches = repository.MatchPatches(branchTip, lineTip, kPatchCommitsPerCheck, cache);

			if (!patches.HasValue())
			{
				if (IsMissing(patches.GetError()))
				{
					return VerdictOf(ContentOutcome::Unknown);
				}

				return std::move(patches).GetError();
			}

			const PatchMatch& match = patches.GetValue();

			if (EveryOwnChangeHasTwin(match))
			{
				return ContentVerdict{.Outcome = ContentOutcome::Merged, .Kind = ContentKind::Rebase, .Twins = match.Twins};
			}

			if (mergeChangesNothing.GetValue() == ContentAnswer::Yes)
			{
				return SquashVerdict(repository, mergeBase.GetValue(), branchTip, lineTip, match);
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
		Result<ContentVerdict> verdict = CheckWithObjectsInMemory(repository, branchTip, lineTip, cache);
		repository.DropObjectsInMemory();
		return verdict;
	}
}
