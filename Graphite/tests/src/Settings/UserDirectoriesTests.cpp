#include <doctest/doctest.h>

#include "Settings/UserDirectories.h"

#include "Core/Platform/Process.h"

#include <filesystem>

// The test runner redirects every user directory before any case runs (see Main.cpp), so a
// setter exercised by another suite writes into build output, never into the real profile.
// These cases hold that promise to account.
namespace {
	bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& root)
	{
		const std::filesystem::path relative = path.lexically_relative(root);
		return !relative.empty() && *relative.begin() != "..";
	}
}

TEST_CASE("Every user file lives under the redirected root while tests run")
{
	const std::filesystem::path root = Graphite::UserDirectories::RedirectedRoot().value();

	CHECK(IsWithin(Graphite::UserDirectories::SettingsFile(), root));
	CHECK(IsWithin(Graphite::UserDirectories::LayoutFile(), root));
	CHECK(IsWithin(Graphite::UserDirectories::ThemesDirectory(), root));
	CHECK(IsWithin(Graphite::UserDirectories::SessionsCacheFile(), root));
	CHECK(IsWithin(Graphite::UserDirectories::LogFile(), root));
	CHECK(IsWithin(Graphite::UserDirectories::TrustedFoldersFile(), root));
	CHECK(IsWithin(Graphite::UserDirectories::ServersDirectory(), root));
	CHECK(IsWithin(Graphite::UserDirectories::ServerTemporaryFilesDirectory(), root));
}

TEST_CASE("No user file resolves into the real profile while tests run")
{
	const std::filesystem::path roaming = Core::RoamingDataDirectory().GetValue();

	CHECK_FALSE(IsWithin(Graphite::UserDirectories::SettingsFile(), roaming));
	CHECK_FALSE(IsWithin(Graphite::UserDirectories::ThemesDirectory(), roaming));
}

TEST_CASE("The directories a file is written into exist before anything writes there")
{
	CHECK(std::filesystem::is_directory(Graphite::UserDirectories::SettingsFile().parent_path()));
	CHECK(std::filesystem::is_directory(Graphite::UserDirectories::SessionsCacheFile().parent_path()));
	CHECK(std::filesystem::is_directory(Graphite::UserDirectories::LogFile().parent_path()));
	CHECK(std::filesystem::is_directory(Graphite::UserDirectories::ServersDirectory()));
	CHECK(std::filesystem::is_directory(Graphite::UserDirectories::ServerTemporaryFilesDirectory()));
}
