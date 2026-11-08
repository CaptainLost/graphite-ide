#include "Workspace/OwnStateChanges.h"

#include "Persistence/FolderStateDirectory.h"

#include "Core/FileSystem/FileSystem.h"

namespace Graphite {

	bool OnlyOwnStateChanged(const std::vector<std::filesystem::path>& directories,
							 const std::filesystem::path& root)
	{
		if (directories.empty() || root.empty())
		{
			return false;
		}

		const std::filesystem::path stateDirectory = FolderStateDirectory(root);

		for (const std::filesystem::path& directory : directories)
		{
			const bool insideStateDirectory = Core::RelativeTo(directory, stateDirectory).has_value();

			if (!insideStateDirectory)
			{
				return false;
			}
		}

		return true;
	}
}
