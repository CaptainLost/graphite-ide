#pragma once

#include "Core/Git/Tag.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// Either one tag or a folder with its tags in order.
	struct TagFolderItem
	{
		std::optional<std::size_t> Tag; // index into the tag list; empty for a folder
		std::string Folder;             // the first segment of the names it holds, without its "/"
		std::vector<std::size_t> Tags;  // a folder's tags, in the order `shown` gave them
	};

	// `shown` holds indices into `tags` newest first, the ones a search kept. A name with a "/" goes into the folder of
	// its first segment, and the folder stands where its newest tag does; a name without one stands alone. Folders nest
	// one level only: a deeper name keeps the rest of its path.
	// No name is read for meaning: "/" makes a folder and nothing else.
	std::vector<TagFolderItem> FoldTags(const std::vector<Core::Tag>& tags, std::span<const std::size_t> shown);

	// The first segment of a tag's name when it has a "/" in it, else empty.
	std::string_view FolderOfTagName(std::string_view name);
}
