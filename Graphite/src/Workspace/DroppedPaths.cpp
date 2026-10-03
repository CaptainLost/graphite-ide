#include "Workspace/DroppedPaths.h"

#include "Core/Base/Ascii.h"
#include "Core/Fs/FileSystem.h"

#include <string>

namespace Graphite {

	namespace {

		bool IsSolution(const std::filesystem::path& path)
		{
			// Through the path's own string rather than a wide comparison, and folded with
			// the ASCII helper the rest of the application folds with: the extensions being
			// matched are ASCII, and a locale-aware fold would answer differently on a
			// machine whose locale disagrees.
			const std::string extension = Core::LoweredExtension(path);

			return extension == ".sln" || extension == ".slnx";
		}

		// Compared as paths rather than as strings, so a sibling whose name merely begins with
		// the root's - "D:/abc" beside a root of "D:/ab" - is outside it, as it is on disk.
		// The root itself counts as inside: reopening the folder already open changes nothing.
		bool IsInside(const std::filesystem::path& path, const std::filesystem::path& root)
		{
			const std::filesystem::path relative = path.lexically_relative(root);

			return !relative.empty() && *relative.begin() != "..";
		}
	}

	DropRouting ClassifyDrop(const std::vector<std::filesystem::path>& paths,
							 const DirectoryPredicate& isDirectory)
	{
		DropRouting routing;

		for (const std::filesystem::path& path : paths)
		{
			const bool directory = isDirectory(path);

			if (directory || IsSolution(path))
			{
				if (routing.Target == DropTarget::None)
				{
					routing.Target = directory ? DropTarget::Folder : DropTarget::Solution;
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
