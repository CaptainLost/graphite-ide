#pragma once

#include "Persistence/PersistedState.h"

#include <filesystem>

namespace Graphite {

	// What of a folder's saved state is to be reopened. The state comes from the folder's own
	// .graphite/workspace.json, so in restricted mode (ADR 0532) whoever wrote the folder wrote
	// it too: a tab there could name any file on the disk, and a diff any path git is asked
	// about. An untrusted folder therefore reopens only the tabs that lie inside it, and the
	// diffs whose path stays inside the repository; the active tab goes with them. Paths are
	// judged lexically, the way WorkspaceTrust judges folders, so "Repo\..\x" is outside Repo.
	//
	// A trusted folder gets everything back, outside it or not. Terminals are left as they are:
	// the terminal rules decide those, because restricted mode starts none of them at all.
	WorkspaceState RestorableState(WorkspaceState stored, const std::filesystem::path& root, bool trusted);
}
