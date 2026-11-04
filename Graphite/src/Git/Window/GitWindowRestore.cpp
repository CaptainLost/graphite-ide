#include "Git/Window/GitWindowRestore.h"

#include "Git/Branches/BranchLookup.h"

namespace Graphite {

	std::optional<std::string> RefToRestore(const GitWindowState& state, const std::vector<Core::Branch>& branches)
	{
		if (state.Ref.empty() || BranchWithFullName(branches, state.Ref) == nullptr)
		{
			return std::nullopt;
		}

		return state.Ref;
	}
}
