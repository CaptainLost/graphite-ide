#pragma once

#include "Persistence/PersistedState.h"

#include <filesystem>

namespace Core {

	class JsonFile;

}

namespace Graphite {

	// Writes into a file that also holds other settings: every key these write is replaced, the
	// rest is left as found. Reading never fails — whatever is missing or unreadable comes back
	// as absent, and a damaged file costs the state it held rather than the launch.
	void WriteGlobalState(const GlobalState& state, Core::JsonFile& file);
	GlobalState ReadGlobalState(const Core::JsonFile& file);

	// Paths under `root` are stored relative to it, so a folder that is moved or cloned elsewhere
	// keeps its tabs.
	void WriteWorkspaceState(const WorkspaceState& state, const std::filesystem::path& root,
							 Core::JsonFile& file);
	WorkspaceState ReadWorkspaceState(const Core::JsonFile& file, const std::filesystem::path& root);
}
