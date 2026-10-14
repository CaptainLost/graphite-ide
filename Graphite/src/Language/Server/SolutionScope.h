#pragma once

#include "Language/Api/LanguageId.h"

#include <filesystem>
#include <map>

namespace Graphite {

	bool LanguageRootsAtSolution(LanguageId language);

	bool DirectoryHoldsSolution(const std::filesystem::path& directory);

	// WARNING: It caches directory listings without a lock, so use it from one worker thread only.
	class SolutionScope
	{
	public:
		SolutionScope(std::filesystem::path root, const std::filesystem::path& selectedSolution);

		const std::filesystem::path& GetFolder() const;

		bool Includes(const std::filesystem::path& file);

	private:
		bool HoldsSolution(const std::filesystem::path& directory);

		std::filesystem::path m_Folder;
		std::map<std::filesystem::path, bool> m_HoldsSolution;
	};
}
