#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Oid.h"
#include "Core/Git/PatchMatch.h"

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

	// Whether the line holds the branch's changes without holding its commits. Rebase when every change
	// of the branch's own has a copy on the line; Squash when merging the branch into the line changes
	// nothing although some change has none. A branch whose changes cancel out is not merged. Unknown,
	// not a failure, when a tip or an object a step needs is missing, or when the line's commits ran
	// past what one check reads. Needs a repository opened with ObjectWrites::InMemory.
	Result<ContentVerdict> CheckMergedByContent(const Repository& repository, const Oid& branchTip, const Oid& lineTip,
												PatchIdCache& cache);
}
