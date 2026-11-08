#include "corepch.h"

#include "Core/Platform/ShellProfiles.h"

#include "Core/Base/PlatformDetection.h"
#include "Core/Base/ScopeExit.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Process.h"

#include <cstdlib>
#include <string_view>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

	namespace {
		constexpr wchar_t kPathEntrySeparator = L';';
		constexpr wchar_t kQuote = L'"';

		// An empty base would turn the tail into a path relative to the working directory, which can
		// exist there by accident, so a missing base gives an empty path instead.
		std::filesystem::path JoinIfPresent(const std::filesystem::path& baseDirectory, const std::filesystem::path& relativeTail)
		{
			if (baseDirectory.empty())
			{
				return {};
			}

			return baseDirectory / relativeTail;
		}

		bool ContainsShellAt(const std::vector<ShellProfile>& shells, const std::filesystem::path& path)
		{
			for (const ShellProfile& shell : shells)
			{
				if (shell.Path == path)
				{
					return true;
				}
			}

			return false;
		}

		void AddIfPresent(std::vector<ShellProfile>& shells, std::string name, const std::filesystem::path& path, std::vector<std::string> arguments)
		{
			if (path.empty() || !PathExists(path))
			{
				return;
			}

			if (ContainsShellAt(shells, path))
			{
				return;
			}

			shells.push_back(ShellProfile{.Name = std::move(name), .Path = path, .Arguments = std::move(arguments)});
		}

		// Installers quote a PATH entry that holds a space, and CreateProcess strips the quotes before
		// searching; kept, they would name a directory that does not exist.
		std::wstring_view WithoutEnclosingQuotes(std::wstring_view pathEntry)
		{
			const bool hasRoomForTwoQuotes = pathEntry.size() >= 2;

			if (!hasRoomForTwoQuotes)
			{
				return pathEntry;
			}

			const bool startsAndEndsWithQuote = pathEntry.front() == kQuote && pathEntry.back() == kQuote;

			if (!startsAndEndsWithQuote)
			{
				return pathEntry;
			}

			const std::size_t lengthInsideQuotes = pathEntry.size() - 2;
			return pathEntry.substr(1, lengthInsideQuotes);
		}

#ifdef CORE_PLATFORM_WINDOWS
		constexpr DWORD kNoOptions = 0;

		// Git for Windows puts git.exe in cmd\ and in mingw64\bin\, one and two levels below the
		// install root that holds bin\bash.exe.
		constexpr int kGitDirectoryAndParentsSearched = 3;

		struct InstallerKey
		{
			HKEY Root;
			const wchar_t* SubKey;
		};

		std::filesystem::path GitBashUnder(const std::filesystem::path& installRoot)
		{
			return installRoot / "bin" / "bash.exe";
		}

		std::filesystem::path ReadRegistryInstallPath(HKEY root, const wchar_t* subKey)
		{
			HKEY openedKey = nullptr;
			const LONG openStatus = ::RegOpenKeyExW(root, subKey, kNoOptions, KEY_READ, &openedKey);

			if (openStatus != ERROR_SUCCESS)
			{
				return {};
			}

			const ScopeExit closeKey{[openedKey] { ::RegCloseKey(openedKey); }};

			// RegQueryValueExW null-terminates a REG_SZ only when the stored value already was, so the
			// zero-filled buffer is offered one wchar_t short and the wchar_t after the value is forced to zero.
			wchar_t installPathBuffer[MAX_PATH] = {};
			DWORD valueByteCount = sizeof(installPathBuffer) - sizeof(wchar_t);
			DWORD valueType = 0;

			const LONG queryStatus = ::RegQueryValueExW(openedKey, L"InstallPath", nullptr, &valueType,
														reinterpret_cast<BYTE*>(installPathBuffer), &valueByteCount);

			if (queryStatus != ERROR_SUCCESS || valueType != REG_SZ)
			{
				return {};
			}

			const std::size_t terminatorIndex = valueByteCount / sizeof(wchar_t);
			installPathBuffer[terminatorIndex] = L'\0';

			return std::filesystem::path{installPathBuffer};
		}

		std::filesystem::path GitBashFromRegistry()
		{
			for (const std::filesystem::path& installRoot : GitForWindowsInstallRoots())
			{
				const std::filesystem::path gitBash = GitBashUnder(installRoot);

				if (PathExists(gitBash))
				{
					return gitBash;
				}
			}

			return {};
		}

		std::filesystem::path GitBashFromPath()
		{
			const std::filesystem::path gitExecutable = FindOnPath("git.exe");

			if (gitExecutable.empty())
			{
				return {};
			}

			std::filesystem::path directory = gitExecutable.parent_path();

			for (int directoriesSearched = 0; directoriesSearched < kGitDirectoryAndParentsSearched && !directory.empty(); ++directoriesSearched)
			{
				const std::filesystem::path gitBash = GitBashUnder(directory);

				if (PathExists(gitBash))
				{
					return gitBash;
				}

				const std::filesystem::path parent = directory.parent_path();
				const bool reachedFilesystemRoot = parent == directory;

				if (reachedFilesystemRoot)
				{
					break;
				}

				directory = parent;
			}

			return {};
		}
#endif
	}

	std::filesystem::path FindOnPath(const std::string& executableFileName)
	{
		const std::optional<std::filesystem::path> pathVariableValue = EnvironmentPath(L"PATH");

		if (!pathVariableValue.has_value())
		{
			return {};
		}

		const std::wstring pathVariable = pathVariableValue->wstring();
		std::size_t entryStart = 0;
		bool entriesRemain = true;

		while (entriesRemain)
		{
			const std::size_t separatorPosition = pathVariable.find(kPathEntrySeparator, entryStart);
			const bool isLastEntry = separatorPosition == std::wstring::npos;
			const std::size_t entryEnd = isLastEntry ? pathVariable.size() : separatorPosition;

			const std::wstring_view entry = WithoutEnclosingQuotes(std::wstring_view{pathVariable}.substr(entryStart, entryEnd - entryStart));
			const std::filesystem::path entryDirectory{std::wstring{entry}};

			// WARNING: A relative entry such as "." names a folder under this process's current directory, which can be
			// whatever folder the user opened, so a program found there is one the folder planted, not one PATH meant.
			if (entryDirectory.is_absolute())
			{
				const std::filesystem::path candidate = entryDirectory / executableFileName;

				if (PathExists(candidate))
				{
					return candidate;
				}
			}

			entriesRemain = !isLastEntry;
			entryStart = entryEnd + 1;
		}

		return {};
	}

	std::vector<std::filesystem::path> GitForWindowsInstallRoots()
	{
		std::vector<std::filesystem::path> installRoots;

#ifdef CORE_PLATFORM_WINDOWS
		const InstallerKey machineWideInstall{.Root = HKEY_LOCAL_MACHINE, .SubKey = L"SOFTWARE\\GitForWindows"};
		const InstallerKey perUserInstallWithoutAdmin{.Root = HKEY_CURRENT_USER, .SubKey = L"SOFTWARE\\GitForWindows"};
		const InstallerKey thirtyTwoBitInstallSeenFromSixtyFourBitProcess{.Root = HKEY_LOCAL_MACHINE, .SubKey = L"SOFTWARE\\WOW6432Node\\GitForWindows"};

		const InstallerKey installerKeys[] = {machineWideInstall, perUserInstallWithoutAdmin, thirtyTwoBitInstallSeenFromSixtyFourBitProcess};

		for (const InstallerKey& installerKey : installerKeys)
		{
			std::filesystem::path installRoot = ReadRegistryInstallPath(installerKey.Root, installerKey.SubKey);

			if (!installRoot.empty())
			{
				installRoots.push_back(std::move(installRoot));
			}
		}
#endif

		return installRoots;
	}

	std::filesystem::path DefaultGitForWindowsRoot()
	{
#ifdef CORE_PLATFORM_WINDOWS
		const std::filesystem::path programFiles = EnvironmentPath(L"ProgramFiles").value_or(std::filesystem::path{});

		return JoinIfPresent(programFiles, "Git");
#else
		return {};
#endif
	}

	std::vector<ShellProfile> DiscoverShells()
	{
		std::vector<ShellProfile> shells;

#ifdef CORE_PLATFORM_WINDOWS
		const std::vector<std::string> noArguments;

		const std::filesystem::path programFiles = EnvironmentPath(L"ProgramFiles").value_or(std::filesystem::path{});
		const std::filesystem::path systemRoot = EnvironmentPath(L"SystemRoot").value_or(std::filesystem::path{});
		const std::filesystem::path systemDirectory = JoinIfPresent(systemRoot, "System32");

		const std::filesystem::path powerShell = JoinIfPresent(programFiles, std::filesystem::path{"PowerShell"} / "7" / "pwsh.exe");
		AddIfPresent(shells, "PowerShell", powerShell, noArguments);

		const std::filesystem::path windowsPowerShell = JoinIfPresent(systemDirectory, std::filesystem::path{"WindowsPowerShell"} / "v1.0" / "powershell.exe");
		AddIfPresent(shells, "Windows PowerShell", windowsPowerShell, noArguments);

		const std::filesystem::path commandPrompt = JoinIfPresent(systemDirectory, "cmd.exe");
		AddIfPresent(shells, "Command Prompt", commandPrompt, noArguments);

		// Git for Windows may live on any drive and folder, and a portable copy writes no registry key,
		// so %ProgramFiles%\Git is only the last guess, after the installer's key and the git.exe on PATH.
		std::filesystem::path gitBash = GitBashFromRegistry();
		if (gitBash.empty())
		{
			gitBash = GitBashFromPath();
		}

		const std::filesystem::path defaultGitRoot = DefaultGitForWindowsRoot();
		if (gitBash.empty() && !defaultGitRoot.empty())
		{
			gitBash = GitBashUnder(defaultGitRoot);
		}

		// Git Bash reads its profile only as a login shell; without --login the prompt is a bare
		// "bash-5.2$" with no environment.
		const std::vector<std::string> loginInteractiveArguments{"--login", "-i"};
		AddIfPresent(shells, "Git Bash", gitBash, loginInteractiveArguments);

		const std::filesystem::path windowsSubsystemForLinux = JoinIfPresent(systemDirectory, "wsl.exe");
		AddIfPresent(shells, "WSL", windowsSubsystemForLinux, noArguments);
#else
		const std::vector<std::string> interactiveArguments{"-i"};

		const char* const userShell = std::getenv("SHELL");
		const std::filesystem::path userShellPath = userShell == nullptr ? std::filesystem::path{} : std::filesystem::path{userShell};
		AddIfPresent(shells, "Default shell", userShellPath, interactiveArguments);
		AddIfPresent(shells, "bash", "/bin/bash", interactiveArguments);
		AddIfPresent(shells, "sh", "/bin/sh", interactiveArguments);
#endif

		return shells;
	}
}
