#include "corepch.h"

#include "Core/Platform/ShellProfiles.h"

#include "Core/Base/PlatformDetection.h"

#include <array>
#include <cstdlib>
#include <string_view>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

	namespace {
		std::string EnvironmentString(const char* name)
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

			std::string result = value;
			std::free(value);
			return result;
#else
			const char* value = std::getenv(name);

			return value == nullptr ? std::string{} : std::string{value};
#endif
		}

		std::filesystem::path EnvironmentPath(const char* name)
		{
			const std::string value = EnvironmentString(name);

			return value.empty() ? std::filesystem::path{} : std::filesystem::path{value};
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

#ifdef CORE_PLATFORM_WINDOWS
		// The registry key an installer writes is preferred over guessing a directory: Git
		// for Windows lets the user pick any install drive and folder, so "%ProgramFiles%\Git"
		// is only ever a guess that happens to be right on a default install. The key is the
		// one place that answers correctly no matter where the user put it.
		std::filesystem::path ReadRegistryInstallPath(HKEY root, const wchar_t* subKey)
		{
			HKEY key = nullptr;

			if (::RegOpenKeyExW(root, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
			{
				return {};
			}

			wchar_t buffer[MAX_PATH] = {};
			DWORD size = sizeof(buffer) - sizeof(wchar_t); // leave room to force termination
			DWORD type = 0;

			const LONG status = ::RegQueryValueExW(key, L"InstallPath", nullptr, &type,
													reinterpret_cast<BYTE*>(buffer), &size);

			::RegCloseKey(key);

			if (status != ERROR_SUCCESS || type != REG_SZ)
			{
				return {};
			}

			// RegQueryValueExW only null-terminates a REG_SZ if the source value already was;
			// the buffer is one wchar_t larger than what was requested, and stays zero-filled,
			// so indexing at the requested size is always in bounds and always a terminator.
			buffer[size / sizeof(wchar_t)] = L'\0';

			return std::filesystem::path{buffer};
		}

		// Tries every place Git for Windows' installer might have recorded InstallPath: the
		// machine-wide key, the per-user key (a non-admin install), and the WOW6432Node
		// mirror that a 32-bit build of Git lands in when seen from a 64-bit process.
		std::filesystem::path GitBashFromRegistry()
		{
			struct Candidate
			{
				HKEY Root;
				const wchar_t* SubKey;
			};

			const Candidate candidates[] = {
				{HKEY_LOCAL_MACHINE, L"SOFTWARE\\GitForWindows"},
				{HKEY_CURRENT_USER, L"SOFTWARE\\GitForWindows"},
				{HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GitForWindows"},
			};

			for (const Candidate& candidate : candidates)
			{
				const std::filesystem::path installPath =
					ReadRegistryInstallPath(candidate.Root, candidate.SubKey);

				if (installPath.empty())
				{
					continue;
				}

				const std::filesystem::path bash = installPath / "bin" / "bash.exe";
				std::error_code status;

				if (std::filesystem::exists(bash, status))
				{
					return bash;
				}
			}

			return {};
		}

		// Git for Windows places git.exe in two different spots below the install root —
		// "cmd\git.exe" and "mingw64\bin\git.exe" — at different depths, with "bin\bash.exe"
		// a sibling of whichever the shim on PATH resolves to. Rather than assume one fixed
		// depth, climb a few levels from wherever PATH found git.exe and check each for the
		// "bin\bash.exe" that would sit beside it at the real install root.
		std::filesystem::path GitBashFromPath()
		{
			const std::filesystem::path gitExecutable = FindOnPath("git.exe");

			if (gitExecutable.empty())
			{
				return {};
			}

			std::filesystem::path directory = gitExecutable.parent_path();

			for (int level = 0; level < 3 && !directory.empty(); ++level)
			{
				const std::filesystem::path candidate = directory / "bin" / "bash.exe";
				std::error_code status;

				if (std::filesystem::exists(candidate, status))
				{
					return candidate;
				}

				const std::filesystem::path parent = directory.parent_path();
				if (parent == directory)
				{
					break;
				}

				directory = parent;
			}

			return {};
		}
#endif
	}

	// An installer may write a PATH entry with quotes around it — usually one containing a
	// space. CreateProcess strips them before it resolves anything, so a segment used
	// verbatim here would name a directory whose first and last characters are quote marks
	// and match nothing at all.
	std::string_view StripQuotes(std::string_view segment)
	{
		if (segment.size() >= 2 && segment.front() == '"' && segment.back() == '"')
		{
			return segment.substr(1, segment.size() - 2);
		}

		return segment;
	}

	// Finds an executable the same way CreateProcess resolves a bare command name: by
	// walking PATH in order.
	std::filesystem::path FindOnPath(const std::string& executableName)
	{
		const std::string pathVariable = EnvironmentString("PATH");

		std::size_t start = 0;
		while (start <= pathVariable.size())
		{
			std::size_t end = pathVariable.find(';', start);
			if (end == std::string::npos)
			{
				end = pathVariable.size();
			}

			const std::string_view segment = StripQuotes(
				std::string_view{pathVariable}.substr(start, end - start));

			if (!segment.empty())
			{
				const std::filesystem::path candidate =
					std::filesystem::path{std::string{segment}} / executableName;
				std::error_code status;

				if (std::filesystem::exists(candidate, status))
				{
					return candidate;
				}
			}

			start = end + 1;
		}

		return {};
	}

	std::vector<ShellProfile> DiscoverShells()
	{
		std::vector<ShellProfile> shells;

#ifdef CORE_PLATFORM_WINDOWS
		const std::filesystem::path programFiles = EnvironmentPath("ProgramFiles");
		const std::filesystem::path systemRoot = EnvironmentPath("SystemRoot");
		const std::filesystem::path system = JoinIfPresent(systemRoot, "System32");

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
		// without it the prompt is a bare "bash-5.2$" with no environment. Three mechanisms
		// are tried in order of trust: the registry key the installer itself writes (correct
		// regardless of which drive Git lives on), then a PATH search (covers portable and
		// unpackaged installs the registry never heard of), then the old %ProgramFiles%
		// guess as a last resort that costs nothing and still covers the ordinary default
		// install. The first mechanism that finds a bash.exe wins and the rest are skipped, so
		// exactly one of them ever contributes a path; AddIfPresent still de-duplicates, but
		// against this list that is defence for a future change rather than something today's
		// code exercises.
		std::filesystem::path gitBash = GitBashFromRegistry();
		if (gitBash.empty())
		{
			gitBash = GitBashFromPath();
		}
		if (gitBash.empty())
		{
			gitBash = JoinIfPresent(programFiles, std::filesystem::path{"Git"} / "bin" /
														"bash.exe");
		}
		AddIfPresent(shells, "Git Bash", gitBash, {"--login", "-i"});

		AddIfPresent(shells, "WSL", JoinIfPresent(system, "wsl.exe"), {});
#else
		// /etc/shells lists what the machine offers, and SHELL names the user's own.
		AddIfPresent(shells, "Default shell", EnvironmentPath("SHELL"), {"-i"});
		AddIfPresent(shells, "bash", "/bin/bash", {"-i"});
		AddIfPresent(shells, "sh", "/bin/sh", {"-i"});
#endif

		return shells;
	}
}
