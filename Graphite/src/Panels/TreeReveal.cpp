#include "Panels/TreeReveal.h"

#include <algorithm>
#include <iterator>

namespace Graphite {

	std::vector<std::filesystem::path> ExpandToward(Core::FileTree& tree,
													const std::filesystem::path& file)
	{
		Core::FileNode* node = &tree.GetRoot();
		const std::filesystem::path relative = file.lexically_relative(node->Path);

		if (relative.empty() || *relative.begin() == "..")
			return {};

		std::vector<std::filesystem::path> opened;

		// Every component but the last names a folder; the last is the file itself.
		for (auto component = relative.begin(); component != relative.end(); ++component)
		{
			// A folder row, so Expand cannot fail; an unreadable folder it logs and marks.
			static_cast<void>(tree.Expand(*node));
			opened.push_back(node->Path);

			if (std::next(component) == relative.end())
				break;

			const auto child = std::find_if(node->Children.begin(), node->Children.end(),
											[&component](const Core::FileNode& candidate) {
												return candidate.IsDirectory &&
													   candidate.Path.filename() == *component;
											});

			if (child == node->Children.end())
				break;

			node = &*child;
		}

		return opened;
	}

	bool HoldsFile(const ContentFolder& folder, const std::filesystem::path& file)
	{
		const bool here = std::any_of(folder.Files.begin(), folder.Files.end(),
									  [&file](const ContentFile& entry) { return entry.Path == file; });

		return here || std::any_of(folder.Folders.begin(), folder.Folders.end(),
								   [&file](const ContentFolder& child) { return HoldsFile(child, file); });
	}

	bool HoldsFile(const Project& project, const std::filesystem::path& file)
	{
		return project.ContentRead && HoldsFile(project.Content.Root, file);
	}

	bool HoldsFile(const Folder& folder, const std::filesystem::path& file)
	{
		const bool here = std::any_of(folder.Projects.begin(), folder.Projects.end(),
									  [&file](const Project& project) { return HoldsFile(project, file); });

		return here || std::any_of(folder.Folders.begin(), folder.Folders.end(),
								   [&file](const Folder& child) { return HoldsFile(child, file); });
	}
}
