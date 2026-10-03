#include <doctest/doctest.h>

#include "Core/Base/PlatformDetection.h"
#include "Core/Platform/ShellProfiles.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

	// PATH is read the platform's way. MSVC rejects std::getenv in favour of _dupenv_s, which
	// owns its buffer; everywhere else getenv is the only spelling there is. The separator
	// differs along the same line, so both halves are needed for this file to compile — and to
	// mean the same thing — on either target.
#ifdef CORE_PLATFORM_WINDOWS
	constexpr char PathSeparator = ';';
#else
	constexpr char PathSeparator = ':';
#endif

	std::string PathVariable()
	{
#ifdef CORE_PLATFORM_WINDOWS
		char* value = nullptr;
		std::size_t length = 0;

		if (_dupenv_s(&value, &length, "PATH") != 0 || value == nullptr)
		{
			return {};
		}

		std::string result = value;
		std::free(value);
		return result;
#else
		const char* value = std::getenv("PATH");

		return value == nullptr ? std::string{} : std::string{value};
#endif
	}

	// A minimal, test-local PATH search — deliberately not the production implementation —
	// so this test proves the outcome (git.exe on PATH implies a Git Bash profile) rather
	// than re-running the code under test against itself.
	bool IsOnPath(const std::string& executableName)
	{
		const std::string path = PathVariable();

		std::size_t start = 0;
		while (start <= path.size())
		{
			std::size_t end = path.find(PathSeparator, start);
			if (end == std::string::npos)
			{
				end = path.size();
			}

			if (end > start)
			{
				std::error_code status;
				const std::filesystem::path candidate =
					std::filesystem::path{path.substr(start, end - start)} / executableName;

				if (std::filesystem::exists(candidate, status))
				{
					return true;
				}
			}

			start = end + 1;
		}

		return false;
	}
}

TEST_CASE("DiscoverShells finds at least one shell on this machine")
{
	const std::vector<Core::ShellProfile> shells = Core::DiscoverShells();

	REQUIRE_FALSE(shells.empty());
}

TEST_CASE("DiscoverShells returns paths that exist")
{
	for (const Core::ShellProfile& shell : Core::DiscoverShells())
	{
		CHECK(std::filesystem::exists(shell.Path));
	}
}

TEST_CASE("DiscoverShells names every shell it returns")
{
	for (const Core::ShellProfile& shell : Core::DiscoverShells())
	{
		CHECK_FALSE(shell.Name.empty());
	}
}

TEST_CASE("DiscoverShells returns each shell once")
{
	const std::vector<Core::ShellProfile> shells = Core::DiscoverShells();

	for (std::size_t outer = 0; outer < shells.size(); ++outer)
	{
		for (std::size_t inner = outer + 1; inner < shells.size(); ++inner)
		{
			CHECK(shells[outer].Path != shells[inner].Path);
		}
	}
}

TEST_CASE("DiscoverShells finds Git Bash when git.exe is on PATH")
{
	// Conditional on the environment rather than hard-coding an install location, so it
	// still proves the mechanism (registry lookup, or the PATH-derived fallback) on a
	// machine where Git lives somewhere other than %ProgramFiles%\Git — such as this one. The
	// same condition skips the case on a platform that has no git.exe to find, where
	// DiscoverShells offers no Git Bash profile by construction.
	if (!IsOnPath("git.exe"))
	{
		return;
	}

	const std::vector<Core::ShellProfile> shells = Core::DiscoverShells();
	const bool foundGitBash = std::any_of(
		shells.begin(), shells.end(),
		[](const Core::ShellProfile& shell) { return shell.Name == "Git Bash"; });

	CHECK(foundGitBash);
}
