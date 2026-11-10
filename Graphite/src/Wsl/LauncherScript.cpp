#include "Wsl/LauncherScript.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/WslSharePath.h"
#include "Core/Remote/AgentDeployment.h"

#include <chrono>

namespace Graphite {

	namespace {

		// Single quotes keep every character as it is in sh; a quote inside ends the quoting,
		// adds an escaped quote and starts it again.
		std::string SingleQuoted(std::string_view text)
		{
			std::string quoted = "'";

			for (const char character : text)
			{
				if (character == '\'')
				{
					quoted += "'\\''";
					continue;
				}

				quoted.push_back(character);
			}

			quoted.push_back('\'');
			return quoted;
		}
	}

	std::string LauncherScript(std::string_view graphiteLinuxPath)
	{
		return "#!/bin/sh\n"
			   "# Opens a folder in Graphite. Written by Graphite; it rewrites this file when it moves.\n"
			   "target=$(realpath -- \"${1:-.}\") || exit 1\n" +
			   SingleQuoted(graphiteLinuxPath) + " \"$(wslpath -w \"$target\")\" >/dev/null 2>&1 &\n";
	}

	void InstallLauncher(const std::string& distro, Core::AgentConnection& connection, const std::filesystem::path& graphiteExecutable)
	{
		constexpr std::chrono::seconds kTimeout{30};

		const Core::Result<std::string> graphiteInDistro = Core::LinuxPathOf(distro, graphiteExecutable);
		const std::string home = connection.GetHome();

		if (!graphiteInDistro || home.empty())
		{
			APP_WARN("Cannot install the graphite command into {}: {}", distro,
					 graphiteInDistro ? std::string{"its home is unknown"} : graphiteInDistro.GetError().Message);
			return;
		}

		const std::string script = LauncherScript(graphiteInDistro.GetValue());
		const std::string folder = home + "/.local/bin";
		const std::string command = folder + "/graphite";

		const Core::Result<std::string> existing = Core::ReadFileBytes(Core::ToWslSharePath(Core::WslSharePath{distro, command}));

		if (existing && existing.GetValue() == script)
		{
			return;
		}

		static_cast<void>(connection.SendAndWait("makeDirectory", {{"path", folder}}, {}, kTimeout));
		const Core::Result<Core::AgentReply> written =
			connection.SendAndWait("writeFile", {{"path", command}, {"executable", true}}, script, kTimeout);

		if (!written)
		{
			APP_WARN("Cannot install the graphite command into {}: {}", distro, written.GetError().Message);
			return;
		}

		APP_INFO("Installed the graphite command into {} at {}", distro, command);
	}
}
