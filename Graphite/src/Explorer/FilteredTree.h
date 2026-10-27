#pragma once

#include "Explorer/FolderListing.h"

#include "Core/FileSystem/FileTree.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	using ListedEntryFilter = std::function<bool(const ListedEntry& entry)>;

	Core::FileNode BuildFilteredTree(const Core::FileNode& root, const std::vector<ListedEntry>& entries,
									 const ListedEntryFilter& passes);

	enum class ListingFreshness
	{
		UpToDate,
		Refreshing
	};

	std::string NoMatchingFilesText(std::string_view filter, ListingFreshness freshness);
}
