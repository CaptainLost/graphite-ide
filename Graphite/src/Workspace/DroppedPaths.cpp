#include "Workspace/DroppedPaths.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		bool IsSolution(const std::filesystem::path& path)
		{
			const std::string extension = Core::LoweredExtension(path);

			return extension == ".sln" || extension == ".slnx";
		}

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

			if (pathIsDirectory || IsSolution(path))
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
