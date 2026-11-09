#include "corepch.h"

#include "Session/AgentInstall.h"

#include <system_error>

namespace Agent {

	std::string VersionOf(const std::filesystem::path& self)
	{
		return self.parent_path().filename().string(); // not a path conversion: Linux paths are their bytes
	}

	void RemoveOtherVersions(const std::filesystem::path& self)
	{
		const std::filesystem::path ownVersion = self.parent_path();
		const std::filesystem::path versions = ownVersion.parent_path();

		const bool isInstalled = versions.filename() == "agent" && versions.parent_path().filename() == ".graphite";

		if (!isInstalled)
		{
			return;
		}

		std::error_code error;

		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{versions, error})
		{
			if (entry.path() != ownVersion)
			{
				std::error_code ignored;
				std::filesystem::remove_all(entry.path(), ignored);
			}
		}
	}
}
