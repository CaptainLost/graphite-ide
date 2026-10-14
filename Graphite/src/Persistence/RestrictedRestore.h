#pragma once

#include "Persistence/PersistedState.h"

#include <filesystem>

namespace Graphite {

	WorkspaceState RestorableState(WorkspaceState stored, const std::filesystem::path& root, bool trusted);
}
