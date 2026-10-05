#pragma once

#include "Persistence/PersistedState.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Core {

	class JsonFile;

}

namespace Graphite {

	// How every stored value in graphite.json and workspace.json is written and read back.

	// "1" or "0".
	inline std::string Flag(bool value)
	{
		return value ? "1" : "0";
	}

	// The flag Flag wrote, or `fallback` for anything else — absent, empty or hand-edited. The
	// fallback is the setting's default and is stated at each call site.
	inline bool ReadFlag(std::string_view stored, bool fallback)
	{
		if (stored == "1")
		{
			return true;
		}

		if (stored == "0")
		{
			return false;
		}

		return fallback;
	}

	// Removes every key that starts with `prefix`.
	void RemoveKeysStartingWith(Core::JsonFile& file, std::string_view prefix);

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
