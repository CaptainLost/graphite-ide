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
	const auto directory = ExecutableDirectory();

	REQUIRE(directory.HasValue());
	CHECK(std::filesystem::is_directory(directory.GetValue()));

	// Naming the binary rather than just checking for a directory: a function that
	// returned the working directory would pass the weaker assertion, and the working
	// directory is exactly what this exists to stop using.
	CHECK(std::filesystem::exists(directory.GetValue() / "CoreTests.exe"));
}

TEST_CASE("The executable directory is absolute")
{
	const auto directory = ExecutableDirectory();

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

TEST_CASE("The roaming data directory is the profile's AppData and exists")
{
	const Core::Result<std::filesystem::path> roaming = Core::RoamingDataDirectory();

	REQUIRE(roaming.HasValue());
	CHECK(roaming.GetValue().is_absolute());
	CHECK(std::filesystem::is_directory(roaming.GetValue()));
	CHECK(roaming.GetValue() == Core::EnvironmentPath(L"APPDATA").value());
}

TEST_CASE("The local data directory is the profile's LocalAppData and exists")
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
	REQUIRE(_wputenv_s(L"GRAPHITE_TESTS_PATH", L"C:\\Users\\Micha\u0142") == 0);

	CHECK(Core::EnvironmentPath(L"GRAPHITE_TESTS_PATH").value()
		  == std::filesystem::path{L"C:\\Users\\Micha\u0142"});

	_wputenv_s(L"GRAPHITE_TESTS_PATH", L"");
}

TEST_CASE("A program on PATH is found and a missing one is not")
{
	CHECK_FALSE(Core::FindOnPath("cmd.exe").empty());
	CHECK(Core::FindOnPath("graphite-no-such-program.exe").empty());
}

TEST_CASE("A program in a non-ANSI directory on PATH is found")
{
	namespace fs = std::filesystem;
	const fs::path directory = fs::temp_directory_path() / L"graphite-中文-\U0001F600-path";
	std::error_code status;
	fs::create_directories(directory, status);
	REQUIRE_FALSE(status);

	{
		std::ofstream program{directory / L"graphite-wide-path-probe.exe"};
		REQUIRE(program.is_open());
	}

	const std::optional<fs::path> original = Core::EnvironmentPath(L"PATH");
	REQUIRE(original.has_value());
	REQUIRE(_wputenv_s(L"PATH", directory.wstring().c_str()) == 0);

	const fs::path found = Core::FindOnPath("graphite-wide-path-probe.exe");

	// Restore before asserting so a failure cannot leave later tests with a broken PATH.
	const errno_t restored = _wputenv_s(L"PATH", original->wstring().c_str());
	fs::remove_all(directory, status);

	CHECK(restored == 0);
	CHECK(found == directory / L"graphite-wide-path-probe.exe");
}
