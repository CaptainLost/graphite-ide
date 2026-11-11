#include "Wsl/WslShell.h"

#include "Core/FileSystem/WslSharePath.h"

#include <optional>

namespace Graphite {

	TerminalLaunch ShellFor(const Core::ShellProfile& chosen, const std::filesystem::path& directory, const std::filesystem::path& wslExecutable)
	{
		const std::optional<Core::WslSharePath> place = Core::ParseWslSharePath(directory);

		if (!place)
		{
			return TerminalLaunch{.Profile = chosen, .WorkingDirectory = directory};
		}

		return TerminalLaunch{.Profile = Core::ShellProfile{.Name = place->Distro,
															.Path = wslExecutable,
															.Arguments = {"-d", place->Distro, "--cd", place->LinuxPath}},
							  .WorkingDirectory = wslExecutable.parent_path()};
	}
}
