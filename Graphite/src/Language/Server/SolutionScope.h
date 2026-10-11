#pragma once

#include "Language/Api/LanguageId.h"

#include <filesystem>
#include <map>

namespace Graphite {

	bool LanguageRootsAtSolution(LanguageId language);

	// Lists the one directory, every time.
	bool DirectoryHoldsSolution(const std::filesystem::path& directory);

	// Lists directories and keeps what it found: use it on a worker, and from one thread.
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
