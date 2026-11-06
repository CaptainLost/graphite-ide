#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Content/ContentThresholds.h"
#include "Core/Git/Oid.h"
#include "Core/Git/PatchMatch.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace Core {

	class Repository;

	enum class ContentKind
	{
		Squash,
		Rebase,
	};

	enum class ContentOutcome
	{
		Merged,
		NotMerged,
		Unknown,
	};

	struct ContentVerdict
	{
		ContentOutcome Outcome = ContentOutcome::NotMerged;
		ContentKind Kind = ContentKind::Squash; // when Merged
		std::optional<Oid> Landing;             // Squash: the line commit holding the change
		std::vector<PatchTwin> Twins;           // own commit -> its copy on the line

		friend bool operator==(const ContentVerdict&, const ContentVerdict&) = default;
	};

	// How far one check reads. Only a test asks for less than the thresholds.
	struct ContentBudgets
	{
		std::size_t LinePatches = kPatchCommitsPerCheck;  // line commits whose patch the walk reads
		std::size_t LandingChain = kPatchCommitsPerCheck; // first-parent line commits searched for a squash's landing
		std::size_t LandingTries = kLandingTries;         // merges tried as the landing
	};

	// Whether the line holds the branch's changes without holding its commits. Rebase when every change
	// of the branch's own has a copy on the line; Squash when merging the branch into the line changes
	// nothing although some change has none. A branch whose changes cancel out is not merged. Unknown,
	// not a failure, when a tip or an object a step needs is missing, or when the line's commits ran
	// past what one check reads. Needs a repository opened with ObjectWrites::InMemory.
	Result<ContentVerdict> CheckMergedByContent(const Repository& repository, const Oid& branchTip, const Oid& lineTip,
												PatchIdCache& cache);

	Result<ContentVerdict> CheckMergedByContent(const Repository& repository, const Oid& branchTip, const Oid& lineTip,
												PatchIdCache& cache, const ContentBudgets& budgets);
}
