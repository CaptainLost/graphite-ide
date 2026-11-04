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
}
