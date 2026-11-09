#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/Result.h"
#include "Core/Remote/AgentPipe.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Core {

	// The first 16 hex digits of the binary's SHA-256: the folder an agent build is installed in.
	std::string AgentVersionOf(std::string_view binaryBytes);

	struct AgentInstall
	{
		// As the distro calls itself ($WSL_DISTRO_NAME).
		std::string Distro;
		std::string Home;
		std::string Version;
		// ~/.graphite/agent/<version>/graphite-agent, as the distro sees it.
		std::string LinuxPath;
		// The same file through \\wsl.localhost\<distro>\.
		std::filesystem::path SharePath;
	};

	AgentInstall AgentInstallFor(const std::string& distro, const std::string& home, const std::string& version);

	// \\wsl.localhost\<distro><linuxPath>, with Windows separators.
	std::filesystem::path SharePathOf(const std::string& distro, std::string_view linuxPath);

	// wsl.exe runs a command in the default distro.
	bool IsWslAvailable();

	// Asks the distro (the default one when distro is empty) for its name and $HOME, copies the
	// binary through the distro's share unless the same version is there already, marks it
	// executable, and says where it is.
	Result<AgentInstall> InstallAgent(const std::string& distro, const std::filesystem::path& agentBinary);

	// wsl.exe -d <distro> --exec <LinuxPath>, as a child process whose stdin and stdout are the pipe.
	Result<Scope<AgentPipe>> StartAgent(const AgentInstall& install);

	// graphite-agent beside the running executable, where the build copies it.
	std::filesystem::path AgentBinaryBesideExecutable();
}
