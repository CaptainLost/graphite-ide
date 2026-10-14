#pragma once

#include "Persistence/PersistedState.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Core {

	class JsonFile;

}

namespace Graphite {

	inline constexpr std::string_view kStoredTrue = "1";
	inline constexpr std::string_view kStoredFalse = "0";

	inline std::string Flag(bool value)
	{
		return std::string{value ? kStoredTrue : kStoredFalse};
	}

	inline bool ReadFlag(std::string_view stored, bool fallback)
	{
		if (stored == kStoredTrue)
		{
			return true;
		}

		if (stored == kStoredFalse)
		{
			return false;
		}

		return fallback;
	}

	void RemoveKeysStartingWith(Core::JsonFile& file, std::string_view prefix);

	void WriteGlobalState(const GlobalState& state, Core::JsonFile& file);
	GlobalState ReadGlobalState(const Core::JsonFile& file);

	void WriteWorkspaceState(const WorkspaceState& state, const std::filesystem::path& root,
							 Core::JsonFile& file);
	WorkspaceState ReadWorkspaceState(const Core::JsonFile& file, const std::filesystem::path& root);
}
