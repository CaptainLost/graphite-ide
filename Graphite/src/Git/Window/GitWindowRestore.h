#pragma once

#include "Core/Git/Ref.h"
#include "Git/Window/GitWindowSelection.h"
#include "Persistence/PersistedState.h"

#include <vector>

namespace Graphite {

	// What a restored state selects once branches are known. A thread of a branch that is gone opens Work;
	// a history of a ref that is gone shows HEAD's history.
	GitWindowSelection SelectionToRestore(const GitWindowState& state, const std::vector<Core::Branch>& branches);
}
