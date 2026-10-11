#pragma once

#include "Explorer/FolderListing.h"

#include "Core/FileSystem/FileTree.h"

#include <functional>
#include <vector>

namespace Graphite {

	using ListedEntryFilter = std::function<bool(const ListedEntry& entry)>;

	// What the file tree shows while it is filtered: the root, the entries that pass, and the
	// folders on the way to them. A folder holding a match is open; a folder that passes by its
	// own name and holds no match is closed and empty.
	Core::FileNode BuildFilteredTree(const Core::FileNode& root, const std::vector<ListedEntry>& entries,
									 const ListedEntryFilter& passes);
}
