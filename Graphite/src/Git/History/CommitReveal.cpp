#include "Git/History/CommitReveal.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <cstddef>

namespace Graphite {

	RevealStep NextRevealStep(const CommitReveal& reveal, const Core::LogPage& page, const HistoryReach& asked)
	{
		const bool pageHoldsCommit = std::ranges::any_of(
			page.Commits, [&reveal](const Core::CommitSummary& commit) { return commit.Id == reveal.Commit; });

		if (pageHoldsCommit)
		{
			return RevealStep::Select;
		}

		// WARNING: Complete alone does not end the loop: an unreadable commit leaves a page incomplete
		// even where the history ran out, and asking for more would read the same page forever. Only a
		// walk that stopped at its reach has more to give.
		const bool walkStoppedAtReach = page.Walked >= std::min(asked.MaxCount, asked.MaxWalk);
		const bool withinWalkBound = page.Walked < Core::kLogWalkLimit;

		if (!page.Complete && walkStoppedAtReach && withinWalkBound)
		{
			return RevealStep::LoadMore;
		}

		return RevealStep::FilterByHash;
	}

	std::string HistoryPathText(const std::filesystem::path& relativePath)
	{
		return Core::ToGenericUtf8(relativePath);
	}
}
