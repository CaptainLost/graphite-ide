#include <doctest/doctest.h>

#include "Core/Git/Command/GitExecutable.h"
#include "Core/Platform/ShellProfiles.h"

#include "ScopedCurrentDirectory.h"
#include "ScopedEnvironmentVariable.h"
#include "TempDirectory.h"

#include <filesystem>
#include <optional>
#include <system_error>
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

TEST_CASE("Git: git is never taken from a PATH entry relative to the current directory")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path planted = temporaryDirectory.MakeFile("git.exe");

	const ScopedCurrentDirectory insideThePlantedGit(temporaryDirectory.GetPath());
	const ScopedEnvironmentVariable currentDirectoryOnly(L"PATH", L".");
	REQUIRE(insideThePlantedGit.WasChanged());
	REQUIRE(currentDirectoryOnly.WasSet());

	const std::optional<std::filesystem::path> found = Core::FindGit();

	if (found.has_value())
	{
		std::error_code compareError;

		CHECK(found->is_absolute());
		CHECK_FALSE(std::filesystem::equivalent(*found, planted, compareError));
	}
}
