#pragma once

#include "Persistence/PersistedState.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace Core {

	class JsonFile;

}

namespace Graphite {

	class WorkspaceSettings
	{
	public:
		explicit WorkspaceSettings(std::filesystem::path root);

		std::string Read(std::string_view key) const;

		void Write(std::string_view key, std::string value);

		WorkspaceState LoadState() const;

		void SaveState(const WorkspaceState& state);

	private:
		void Modify(const std::function<void(Core::JsonFile&)>& edit);

		std::filesystem::path GetFilePath() const;

		std::filesystem::path m_Root;
	};
}