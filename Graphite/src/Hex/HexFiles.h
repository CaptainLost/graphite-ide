#pragma once

#include "Hex/HexFile.h"

#include "Core/Jobs/JobSubmitter.h"

#include <filesystem>
#include <map>
#include <set>

namespace Graphite {

	// The bytes of every open hex tab, by the tab's path. Paths are matched exactly, as the tabs spell them.
	class HexFiles
	{
	public:
		explicit HexFiles(Core::JobSubmitter submit);

		// Made on first use; it reads nothing until its rows are wanted.
		HexFile& For(const std::filesystem::path& path);

		// Whether the bytes of `path` are kept, which For makes them; nothing is made by asking.
		bool Has(const std::filesystem::path& path) const;

		// Drops the bytes of every tab not in `open`.
		void Prune(const std::set<std::filesystem::path>& open);

		// Drops the bytes of one tab that stays open, such as an image that shows as a picture again.
		void Forget(const std::filesystem::path& path);

		void CheckDisk(const std::filesystem::path& path);

		// Once a frame, before any hex view is drawn.
		void Update();

	private:
		Core::JobSubmitter m_Submit;
		std::map<std::filesystem::path, HexFile> m_Files;
	};
}
