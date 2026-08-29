#include "corepch.h"

#include "Core/Platform/ShellProfiles.h"

#include "Core/Base/PlatformDetection.h"

#include <array>
#include <cstdlib>

namespace Core {

	namespace {
		std::filesystem::path EnvironmentPath(const char* name)
		{
			// std::getenv is flagged by MSVC as unsafe in favour of _dupenv_s, which owns its
			// buffer instead of pointing into process-wide state.
#ifdef CORE_PLATFORM_WINDOWS
			char* value = nullptr;
			std::size_t length = 0;

			if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
			{
				return {};
			}

			std::filesystem::path result;
			if (*value != '\0')
			{
				result = value;
			}

			std::free(value);
			return result;
#else
			const char* value = std::getenv(name);

			if (value == nullptr || *value == '\0')
			{
				return {};
			}

			return std::filesystem::path{value};
#endif
		}

		// Appends onto an environment-derived base, but only when that base is actually
		// present. An empty base concatenated with a relative tail ("PowerShell/7/pwsh.exe")
		// is still a valid, non-empty path — one that resolves relative to the current
		// working directory and could accidentally exist there, so it has to be rejected
		// before it ever reaches std::filesystem::exists rather than relying on that check.
		std::filesystem::path JoinIfPresent(const std::filesystem::path& base,
											 const std::filesystem::path& tail)
		{
			if (base.empty())
			{
				return {};
			}

			return base / tail;
		}

		void AddIfPresent(std::vector<ShellProfile>& shells, std::string name,
						  const std::filesystem::path& path, std::vector<std::string> arguments)
		{
			std::error_code status;

			if (path.empty() || !std::filesystem::exists(path, status))
			{
				return;
			}

			for (const ShellProfile& existing : shells)
			{
				if (existing.Path == path)
				{
					return;
				}
			}

			shells.push_back(ShellProfile{std::move(name), path, std::move(arguments)});
		}
	}

	std::vector<ShellProfile> DiscoverShells()
	{
		std::vector<ShellProfile> shells;

#ifdef CORE_PLATFORM_WINDOWS
		const std::filesystem::path programFiles = EnvironmentPath("ProgramFiles");
		const std::filesystem::path systemRoot = EnvironmentPath("SystemRoot");
		const std::filesystem::path system = JoinIfPresent(systemRoot, "System32");
		const std::filesystem::path localAppData = EnvironmentPath("LOCALAPPDATA");

		AddIfPresent(shells, "PowerShell",
					 JoinIfPresent(programFiles, std::filesystem::path{"PowerShell"} / "7" /
													  "pwsh.exe"),
					 {});
		AddIfPresent(shells, "Windows PowerShell",
					 JoinIfPresent(system, std::filesystem::path{"WindowsPowerShell"} / "v1.0" /
											   "powershell.exe"),
					 {});
		AddIfPresent(shells, "Command Prompt", JoinIfPresent(system, "cmd.exe"), {});
		// --login -i is what makes Git Bash read the profile and behave interactively;
		// without it the prompt is a bare "bash-5.2$" with no environment.
		AddIfPresent(shells, "Git Bash",
					 JoinIfPresent(programFiles, std::filesystem::path{"Git"} / "bin" /
													  "bash.exe"),
					 {"--login", "-i"});
		AddIfPresent(shells, "WSL", JoinIfPresent(system, "wsl.exe"), {});
		(void)localAppData;
#else
		// /etc/shells lists what the machine offers, and SHELL names the user's own.
		AddIfPresent(shells, "Default shell", EnvironmentPath("SHELL"), {"-i"});
		AddIfPresent(shells, "bash", "/bin/bash", {"-i"});
		AddIfPresent(shells, "sh", "/bin/sh", {"-i"});
#endif

		return shells;
	}
}
