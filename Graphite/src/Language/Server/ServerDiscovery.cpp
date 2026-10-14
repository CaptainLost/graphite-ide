#include "Language/Server/ServerDiscovery.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/ShellProfiles.h"

namespace Graphite {

	namespace {

		std::filesystem::path ExecutableNameFor(std::string_view command)
		{
			std::filesystem::path name{command};

			if (Core::LoweredExtension(name) != ".exe")
			{
				name += ".exe";
			}

			return name;
		}
	}

	Core::Result<std::filesystem::path> FindServerBinary(std::string_view command,
														 const std::filesystem::path& configured)
	{
		if (!configured.empty() && Core::PathExists(configured))
		{
			return configured;
		}

		const std::filesystem::path executableName = ExecutableNameFor(command);

		if (std::filesystem::path onPath = Core::FindOnPath(Core::ToUtf8(executableName)); !onPath.empty())
		{
			return onPath;
		}

		return Core::Error{Core::ErrorCode::NotFound, "server binary not found: " + std::string{command}};
	}
}
