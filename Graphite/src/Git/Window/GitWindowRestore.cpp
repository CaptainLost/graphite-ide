#include "Git/Window/GitWindowRestore.h"

#include "Git/DefaultBranch.h"

namespace Graphite {

	GitWindowSelection SelectionToRestore(const GitWindowState& state, const std::vector<Core::Branch>& branches)
	{
		const bool branchIsThere = !state.Branch.empty() && BranchWithFullName(branches, state.Branch) != nullptr;

		switch (state.View)
		{
		case GitWindowView::Thread:
			if (branchIsThere)
			{
				return {GitWindowView::Thread, state.Branch};
			}

			return {GitWindowView::Work, {}};
		case GitWindowView::History:
			return {GitWindowView::History, branchIsThere ? state.Branch : std::string{}};
		case GitWindowView::Work:
		case GitWindowView::Changes:
		case GitWindowView::Branches:
			return {state.View, {}};
		}

		return {};
	}
}
