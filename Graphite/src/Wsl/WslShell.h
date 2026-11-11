#pragma once

#include "Core/Platform/ShellProfiles.h"

#include <filesystem>

namespace Graphite {

	// What a terminal starts, and the folder its console starts in.
	struct TerminalLaunch
	{
		Core::ShellProfile Profile;
		std::filesystem::path WorkingDirectory;
	};

	// A terminal in a folder inside WSL is the distro's own login shell, started in that folder:
	// wsl.exe -d <distro> --cd <linux path>, named after the distro, whatever shell was chosen (a
	// Windows shell cannot work in the distro's files). Its console starts in wsl.exe's own folder,
	// not on the share, which may not answer before the distro runs. Any other folder keeps the
	// chosen shell, started there.
	TerminalLaunch ShellFor(const Core::ShellProfile& chosen, const std::filesystem::path& directory, const std::filesystem::path& wslExecutable);
}
