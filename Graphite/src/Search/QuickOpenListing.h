#pragma once

#include "Search/FolderSearch.h"

#include "Core/Jobs/CancellationToken.h"

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace Graphite {

	// Quick open's list of a folder's files, filled on a worker and read on the UI thread.
	struct QuickOpenListing
	{
		std::mutex Mutex;
		std::vector<std::string> Paths;
		bool Done = false;
	};

	inline constexpr std::size_t kLargestQuickOpenListing = 200000;

	// Lists root's files relative to it, without .git and what git ignores, at most
	// kLargestQuickOpenListing of them: through folder when it is searched where it lives, otherwise
	// from disk, handed over in batches so the first matches show before a large folder is read
	// through. A listing that fails lists nothing (it is logged); Done once it ends either way.
	void ListFolderFiles(const std::filesystem::path& root, FolderSearch* folder, const Core::CancellationToken& token, QuickOpenListing& listing);
}
