#include "Workspace/DroppedPaths.h"

#include "Solution/SolutionReader.h"

#include <string_view>

namespace Graphite {

	namespace {

		constexpr std::string_view kParentDirectoryName = "..";

		bool IsInside(const std::filesystem::path& path, const std::filesystem::path& root)
		{
			const std::filesystem::path relative = path.lexically_relative(root);

			return !relative.empty() && *relative.begin() != kParentDirectoryName;
		}
	}

	DropRouting ClassifyDrop(const std::vector<std::filesystem::path>& paths,
							 const DirectoryPredicate& isDirectory)
	{
		DropRouting routing;

		for (const std::filesystem::path& path : paths)
		{
			const bool pathIsDirectory = isDirectory(path);

			if (pathIsDirectory || IsSolutionFile(path))
			{
				if (routing.Target == DropTarget::None)
				{
					routing.Target = pathIsDirectory ? DropTarget::Folder : DropTarget::Solution;
					routing.Root = path;
				}

				continue;
			}

			routing.Files.push_back(path);
		}

		return routing;
	}

	bool ReplacesWorkspace(const DropRouting& routing,
						   const std::filesystem::path& currentRoot)
	{
		if (routing.Target == DropTarget::None || currentRoot.empty())
		{
			return false;
		}

		return !IsInside(routing.Root, currentRoot);
	}
}
