#pragma once

#include "Core/Remote/AgentConnection.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Graphite {

	// The `graphite` command Graphite writes to ~/.local/bin in each distro it connects to: it
	// starts Graphite on the given folder (the current one by default) as Windows names it, so
	// a folder inside the distro opens as \\wsl.localhost\... and one under /mnt/c as C:\...
	std::string LauncherScript(std::string_view graphiteLinuxPath);

	// Writes the graphite command into the distro's ~/.local/bin through its agent, unless the
	// same script is there already. Failures are logged: the command is a convenience, and the
	// folder works without it.
	void InstallLauncher(const std::string& distro, Core::AgentConnection& connection,
						 const std::filesystem::path& graphiteExecutable);
}
