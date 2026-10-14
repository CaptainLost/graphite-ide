#include "Explorer/TreeReveal.h"

#include <iterator>

namespace Graphite {

	namespace {
		Core::FileNode* FindChildFolder(Core::FileNode& parent, const std::filesystem::path& folderName)
		{
			for (Core::FileNode& candidate : parent.Children)
			{
				if (candidate.IsDirectory && candidate.Path.filename() == folderName)
				{
					return &candidate;
				}
			}

			return nullptr;
		}

		bool ListsFile(const ContentFolder& folder, const std::filesystem::path& file)
		{
			for (const ContentFile& entry : folder.Files)
			{
				if (entry.Path == file)
				{
					return true;
				}
			}

			return false;
		}
	}

	std::vector<std::filesystem::path> ExpandToward(Core::FileTree& tree,
													const std::filesystem::path& file)
	{
		Core::FileNode* node = &tree.GetRoot();
		const std::filesystem::path relative = file.lexically_relative(node->Path);

		if (relative.empty() || *relative.begin() == "..")
		{
			return {};
		}

		std::vector<std::filesystem::path> opened;

		for (std::filesystem::path::const_iterator component = relative.begin(); component != relative.end();
			 ++component)
		{
			// A folder row, so Expand cannot fail; an unreadable folder it logs and marks.
			static_cast<void>(tree.Expand(*node));
			opened.push_back(node->Path);

			const bool isFileItself = std::next(component) == relative.end();

			if (isFileItself)
			{
				break;
			}

			Core::FileNode* child = FindChildFolder(*node, *component);

			if (child == nullptr)
			{
				break;
			}

			node = child;
		}

		return opened;
	}

	bool HoldsFile(const ContentFolder& folder, const std::filesystem::path& file)
	{
		if (ListsFile(folder, file))
		{
			return true;
		}

		for (const ContentFolder& child : folder.Folders)
		{
			if (HoldsFile(child, file))
			{
				return true;
			}
		}

		return false;
	}

	bool HoldsFile(const Project& project, const std::filesystem::path& file)
	{
		return project.ContentRead && HoldsFile(project.Content.Root, file);
	}

	bool HoldsFile(const Folder& folder, const std::filesystem::path& file)
	{
		for (const Project& project : folder.Projects)
		{
			if (HoldsFile(project, file))
			{
				return true;
			}
		}

		for (const Folder& child : folder.Folders)
		{
			if (HoldsFile(child, file))
			{
				return true;
			}
		}

		return false;
	}
}
