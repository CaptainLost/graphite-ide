#include <doctest/doctest.h>

#include "Core/Base/PlatformDetection.h"
#include "Core/Platform/ShellProfiles.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

#ifdef CORE_PLATFORM_WINDOWS
	constexpr char kPathEntrySeparator = ';';
#else
	constexpr char kPathEntrySeparator = ':';
#endif

	// MSVC rejects std::getenv as unsafe, so on Windows PATH is read through _dupenv_s, which hands
	// back a buffer the caller frees.
	std::string PathVariable()
	{
#ifdef CORE_PLATFORM_WINDOWS
		char* pathValue = nullptr;
		std::size_t pathValueLength = 0;
		const errno_t readStatus = _dupenv_s(&pathValue, &pathValueLength, "PATH");

		if (readStatus != 0 || pathValue == nullptr)
		{
			return {};
		}

		std::string pathText = pathValue;
		std::free(pathValue);
		return pathText;
#else
		const char* pathValue = std::getenv("PATH");

		return pathValue == nullptr ? std::string{} : std::string{pathValue};
#endif
	}

	bool IsOnPathIndependentlyOfFindOnPath(const std::string& executableName)
	{
		const std::string pathVariable = PathVariable();
		std::size_t entryStart = 0;
		bool entriesRemain = true;

		while (entriesRemain)
		{
			const std::size_t separatorPosition = pathVariable.find(kPathEntrySeparator, entryStart);
			const bool isLastEntry = separatorPosition == std::string::npos;
			const std::size_t entryEnd = isLastEntry ? pathVariable.size() : separatorPosition;
			const bool entryHasCharacters = entryEnd > entryStart;

			if (entryHasCharacters)
			{
				std::error_code ignoredError;
				const std::filesystem::path candidate = std::filesystem::path{pathVariable.substr(entryStart, entryEnd - entryStart)} / executableName;

				if (std::filesystem::exists(candidate, ignoredError))
				{
					return true;
				}
			}

			entriesRemain = !isLastEntry;
			entryStart = entryEnd + 1;
		}

		return false;
	}

	bool ContainsShellNamed(const std::vector<Core::ShellProfile>& shells, const std::string& shellName)
	{
		for (const Core::ShellProfile& shell : shells)
		{
			if (shell.Name == shellName)
			{
				return true;
			}
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

	for (std::size_t earlierIndex = 0; earlierIndex < shells.size(); ++earlierIndex)
	{
		for (std::size_t laterIndex = earlierIndex + 1; laterIndex < shells.size(); ++laterIndex)
		{
			CHECK(shells[earlierIndex].Path != shells[laterIndex].Path);
		}
	}
}

TEST_CASE("DiscoverShells finds Git Bash wherever Git is installed when git.exe is on PATH and skips the check when it is not")
{
	const bool gitIsOnPath = IsOnPathIndependentlyOfFindOnPath("git.exe");

	if (!gitIsOnPath)
	{
		return;
	}

	const std::vector<Core::ShellProfile> shells = Core::DiscoverShells();
	const bool foundGitBash = ContainsShellNamed(shells, "Git Bash");

	CHECK(foundGitBash);
}
