#include "Git/Window/GitWindowRestore.h"

#include "Git/Branches/BranchLookup.h"
#include "Git/Tags/TagLookup.h"

namespace Graphite {

	std::optional<std::string> RefToRestore(const GitWindowState& state, const std::vector<Core::Branch>& branches,
											const std::vector<Core::Tag>& tags)
	{
		if (state.Ref.empty())
		{
			return std::nullopt;
		}

		const bool listed = IsTagName(state.Ref) ? TagWithFullName(tags, state.Ref) != nullptr
												 : BranchWithFullName(branches, state.Ref) != nullptr;

		return listed ? std::optional<std::string>{state.Ref} : std::nullopt;
	}

	bool RestoreWaitsForListing(const GitWindowState& state, RefListing branches, RefListing tags)
	{
		const RefListing holding = IsTagName(state.Ref) ? tags : branches;

		return !state.Ref.empty() && !holding.Listed && !holding.Failed;
	}
}
