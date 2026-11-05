#pragma once

#include "Core/Git/Ref.h"
#include "Persistence/PersistedState.h"

#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// The ref whose history a restored state shows once the branches are known; none, HEAD's history, when the state
	// names none or one that is gone.
	std::optional<std::string> RefToRestore(const GitWindowState& state, const std::vector<Core::Branch>& branches);

	// Whether the branches are in, or will not come.
	struct BranchListing
	{
		bool Listed = false;
		bool Failed = false;
	};

	// A state that names a ref waits for the branches before it is applied, and until then the history names no
	// reference: showing HEAD's meanwhile would draw a history the window then leaves. A failed listing ends the wait
	// on HEAD's history, as a ref that is gone does.
	bool RestoreWaitsForBranches(const GitWindowState& state, BranchListing branches);
}
