#include "corepch.h"

#include "Core/Git/Command/GitExecutable.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/ShellProfiles.h"

namespace Core {

	namespace {
		constexpr const char* kGitFileName = "git.exe";

		// The installer puts cmd\, not mingw64\bin\, on PATH: cmd\git.exe is the git meant to be run from outside Git's
		// own shell.
		std::filesystem::path GitUnder(const std::filesystem::path& installRoot)
		{
			return installRoot / "cmd" / kGitFileName;
		}

		std::optional<std::filesystem::path> GitUnderFirstInstallRoot()
		{
			for (const std::filesystem::path& installRoot : GitForWindowsInstallRoots())
			{
				const std::filesystem::path installedGit = GitUnder(installRoot);

				if (PathExists(installedGit))
				{
					return installedGit;
				}
			}

			return std::nullopt;
		}
	}

	std::optional<std::filesystem::path> FindGit()
	{
		const std::filesystem::path gitOnPath = FindOnPath(kGitFileName);

		if (!gitOnPath.empty())
		{
			return gitOnPath;
		}

		const std::optional<std::filesystem::path> installedGit = GitUnderFirstInstallRoot();

		if (installedGit.has_value())
		{
			return installedGit;
		}

		// A portable copy, or an install whose registry key is gone, may still sit where the installer puts it by default.
		const std::filesystem::path defaultRoot = DefaultGitForWindowsRoot();

		if (defaultRoot.empty())
		{
			return std::nullopt;
		}

		const std::filesystem::path gitAtDefaultRoot = GitUnder(defaultRoot);

		if (!PathExists(gitAtDefaultRoot))
		{
			return std::nullopt;
		}

		return gitAtDefaultRoot;
	}
}
