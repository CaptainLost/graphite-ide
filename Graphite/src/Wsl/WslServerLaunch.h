#pragma once

#include "Language/Server/ServerLaunch.h"

#include "Core/FileSystem/WslSharePath.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Graphite {

	// The program a folder inside WSL runs its server as: the configured one when it is a Linux
	// path; otherwise the one installed into the distro (its share path, run by its Linux path);
	// otherwise the catalogue's executable name, which the distro's login shell finds on PATH. A
	// configured Windows program, or a Windows install, is for Windows folders.
	std::string DistroServerProgram(const ServerDefinition& server, const std::filesystem::path& configured,
									const std::optional<std::filesystem::path>& installed = std::nullopt);

	// A language server for a folder inside WSL, run in the distro (stage 6):
	// wsl.exe -d <distro> --cd <root> --exec /bin/sh -lc 'exec "$0" "$@"' <program> <arguments>.
	// The login shell gives it the PATH the user's own terminals have; its stdio is the server's,
	// and ending wsl.exe ends it. It is spoken to in Linux paths (WslServerUris), gets none of
	// Graphite's Windows environment, and says when the distro has no such program.
	ServerLaunch WslServerLaunch(const ServerDefinition& server, const std::string& program, const ServerOverrides& overrides,
								 const Core::WslSharePath& root, const std::filesystem::path& wslExecutable);
}
