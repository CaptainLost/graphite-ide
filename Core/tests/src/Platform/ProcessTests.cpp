#include "corepch.h"

#include "Core/Platform/Process.h"
#include "Core/Platform/ShellProfiles.h"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>

using namespace Core;

TEST_CASE("The executable directory exists and holds the running test binary")
{
	const Result<std::filesystem::path> directory = ExecutableDirectory();

	REQUIRE(directory.HasValue());
	CHECK(std::filesystem::is_directory(directory.GetValue()));

	// The binary is named because the working directory, which this exists to stop using, would
	// pass a check for a directory alone.
	CHECK(std::filesystem::exists(directory.GetValue() / "CoreTests.exe"));
}

TEST_CASE("The executable directory is absolute")
{
	const Result<std::filesystem::path> directory = ExecutableDirectory();

	REQUIRE(directory.HasValue());
	CHECK(directory.GetValue().is_absolute());
}

TEST_CASE("The home directory is the user's profile and exists")
{
	const Core::Result<std::filesystem::path> home = Core::HomeDirectory();

	REQUIRE(home.HasValue());
	CHECK(std::filesystem::is_directory(home.GetValue()));
	CHECK(home.GetValue() == Core::EnvironmentPath(L"USERPROFILE").value());
}

TEST_CASE("The roaming data directory for settings that follow the user between machines is the profile's AppData and exists")
{
	const Core::Result<std::filesystem::path> roaming = Core::RoamingDataDirectory();

	REQUIRE(roaming.HasValue());
	CHECK(roaming.GetValue().is_absolute());
	CHECK(std::filesystem::is_directory(roaming.GetValue()));
	CHECK(roaming.GetValue() == Core::EnvironmentPath(L"APPDATA").value());
}

TEST_CASE("The local data directory for caches and logs that stay on this machine is the profile's LocalAppData and exists")
{
	const Core::Result<std::filesystem::path> local = Core::LocalDataDirectory();

	REQUIRE(local.HasValue());
	CHECK(local.GetValue().is_absolute());
	CHECK(std::filesystem::is_directory(local.GetValue()));
	CHECK(local.GetValue() == Core::EnvironmentPath(L"LOCALAPPDATA").value());
}

TEST_CASE("An unset environment variable is no path rather than an empty one")
{
	CHECK_FALSE(Core::EnvironmentPath(L"GRAPHITE_TESTS_SURELY_UNSET_VARIABLE").has_value());
}

TEST_CASE("A set environment variable comes back as the path it names, wide characters intact")
{
	const errno_t setStatus = _wputenv_s(L"GRAPHITE_TESTS_PATH", L"C:\\Users\\Micha\u0142");
	REQUIRE(setStatus == 0);

	CHECK(Core::EnvironmentPath(L"GRAPHITE_TESTS_PATH").value() == std::filesystem::path{L"C:\\Users\\Micha\u0142"});

	const wchar_t* const emptyValueRemovesTheVariable = L"";
	_wputenv_s(L"GRAPHITE_TESTS_PATH", emptyValueRemovesTheVariable);
}

TEST_CASE("A program on PATH is found and a missing one is not")
{
	CHECK_FALSE(Core::FindOnPath("cmd.exe").empty());
	CHECK(Core::FindOnPath("graphite-no-such-program.exe").empty());
}

TEST_CASE("A program in a non-ANSI directory on PATH is found")
{
	const std::filesystem::path directory = std::filesystem::temp_directory_path() / L"graphite-中文-\U0001F600-path";
	const std::filesystem::path probeProgram = directory / L"graphite-wide-path-probe.exe";

	std::error_code fileSystemError;
	std::filesystem::create_directories(directory, fileSystemError);
	REQUIRE_FALSE(fileSystemError);

	{
		std::ofstream probeFile{probeProgram};
		REQUIRE(probeFile.is_open());
	}

	const std::optional<std::filesystem::path> originalPath = Core::EnvironmentPath(L"PATH");
	REQUIRE(originalPath.has_value());

	const errno_t setStatus = _wputenv_s(L"PATH", directory.wstring().c_str());
	REQUIRE(setStatus == 0);

	const std::filesystem::path foundProgram = Core::FindOnPath("graphite-wide-path-probe.exe");

	// PATH is restored before anything is asserted, so a failure cannot leave later tests with a
	// broken PATH.
	const errno_t restoreStatus = _wputenv_s(L"PATH", originalPath->wstring().c_str());
	std::filesystem::remove_all(directory, fileSystemError);

	CHECK(restoreStatus == 0);
	CHECK(foundProgram == probeProgram);
}
