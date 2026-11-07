#include "Git/Tags/TagFolders.h"

#include <string_view>
#include <unordered_map>

namespace Graphite {

	std::string_view FolderOfTagName(std::string_view name)
	{
		const std::size_t slash = name.find('/');
		return slash == std::string_view::npos ? std::string_view{} : name.substr(0, slash);
	}

	std::vector<TagFolderItem> FoldTags(const std::vector<Core::Tag>& tags, std::span<const std::size_t> shown)
	{
		std::vector<TagFolderItem> items;
		std::unordered_map<std::string, std::size_t> itemOfFolder;

		for (const std::size_t index : shown)
		{
			const std::string_view folder = FolderOfTagName(tags[index].Name);

			if (folder.empty())
			{
				items.push_back(TagFolderItem{.Tag = index});
				continue;
			}

			const auto found = itemOfFolder.find(std::string(folder));

			if (found != itemOfFolder.end())
			{
				items[found->second].Tags.push_back(index);
				continue;
			}

			itemOfFolder.emplace(std::string(folder), items.size());
			items.push_back(TagFolderItem{.Folder = std::string(folder), .Tags = {index}});
		}

		return items;
	}
}
