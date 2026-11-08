#include <doctest/doctest.h>

#include "Core/Git/Command/GitExecutable.h"
#include "Core/Platform/ShellProfiles.h"

#include "ScopedEnvironmentVariable.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace {

	std::filesystem::path GitUnder(const std::filesystem::path& installRoot)
	{
		return installRoot / "cmd" / "git.exe";
	}

	bool IsUnderAGitForWindowsRoot(const std::filesystem::path& git)
	{
		for (const std::filesystem::path& installRoot : Core::GitForWindowsInstallRoots())
		{
			if (git == GitUnder(installRoot))
			{
				return true;
			}
		}

		return git == GitUnder(Core::DefaultGitForWindowsRoot());
	}
}

TEST_CASE("Git: git is found on PATH or where Git for Windows installs it")
{
	const std::optional<std::filesystem::path> found = Core::FindGit();

	if (!found.has_value())
	{
		MESSAGE("Skipped: git is neither on PATH nor where Git for Windows installs it");
		return;
	}

	CHECK(std::filesystem::exists(*found));
	CHECK(found->filename() == "git.exe");

	const std::filesystem::path onPath = Core::FindOnPath("git.exe");

	if (!onPath.empty())
	{
		CHECK(*found == onPath);
	}

	const ScopedEnvironmentVariable noPath(L"PATH", std::nullopt);
	REQUIRE(noPath.WasSet());

	const std::optional<std::filesystem::path> installed = Core::FindGit();

	if (!installed.has_value())
	{
		MESSAGE("Skipped the half without PATH: this machine's git is found on PATH alone");
		return;
	}

	CHECK(std::filesystem::exists(*installed));
	CHECK(IsUnderAGitForWindowsRoot(*installed));
}
