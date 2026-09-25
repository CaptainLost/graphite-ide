#include "Workspace/OwnStateChanges.h"

#include "Core/Fs/FileSystem.h"

#include <algorithm>

namespace Graphite {

	bool OnlyOwnStateChanged(const std::vector<std::filesystem::path>& directories,
							 const std::filesystem::path& root)
	{
		if (directories.empty() || root.empty())
		{
			return false;
		}

		// The same directory WorkspaceSettings writes workspace.json into and AnalysisStore keeps
		// its records under. Compared element by element, so a sibling that only begins with the
		// same letters is not taken for it.
		const std::filesystem::path stateDirectory = root / ".graphite";

		return std::all_of(directories.begin(), directories.end(),
						   [&stateDirectory](const std::filesystem::path& directory) {
							   return Core::RelativeTo(directory, stateDirectory).has_value();
						   });
	}
}
