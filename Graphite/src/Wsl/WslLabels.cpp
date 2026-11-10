#include "Wsl/WslLabels.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/WslSharePath.h"

namespace Graphite {

	std::string FolderDisplayName(const std::filesystem::path& folder)
	{
		if (const std::optional<Core::WslSharePath> place = Core::ParseWslSharePath(folder))
		{
			return place->Distro + ": " + place->LinuxPath;
		}

		return Core::ToUtf8(folder);
	}

	std::optional<std::string> WslFolderTitle(const std::filesystem::path& folder)
	{
		const std::optional<Core::WslSharePath> place = Core::ParseWslSharePath(folder);

		if (!place)
		{
			return std::nullopt;
		}

		const std::size_t lastSeparator = place->LinuxPath.find_last_of('/');
		const std::string name = lastSeparator + 1 < place->LinuxPath.size() ? place->LinuxPath.substr(lastSeparator + 1) : "/";

		return place->Distro + ": " + name;
	}
}
