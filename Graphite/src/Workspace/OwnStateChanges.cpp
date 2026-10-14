#include "Workspace/OwnStateChanges.h"

#include "Core/FileSystem/FileSystem.h"

#include <string_view>

namespace Graphite {

	namespace {

		constexpr std::string_view kStateDirectoryName = ".graphite";
	}

	bool OnlyOwnStateChanged(const std::vector<std::filesystem::path>& directories,
							 const std::filesystem::path& root)
	{
		if (directories.empty() || root.empty())
		{
			return false;
		}

		const std::filesystem::path stateDirectory = root / kStateDirectoryName;

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
