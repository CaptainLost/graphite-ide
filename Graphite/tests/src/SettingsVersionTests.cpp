#include <doctest/doctest.h>

#include "Settings/SettingsVersion.h"

#include "Core/Base/JsonFile.h"

#include <string>

TEST_CASE("A settings file with no version is stamped with the current one")
{
	Core::JsonFile file;

	CHECK(Graphite::StampSettingsVersion(file));
	CHECK(file.Get("settingsVersion") == std::to_string(Graphite::kSettingsVersion));
}

TEST_CASE("A settings file at the current version stays writable")
{
	Core::JsonFile file;
	file.Set("settingsVersion", std::to_string(Graphite::kSettingsVersion));

	CHECK(Graphite::StampSettingsVersion(file));
	CHECK(file.Get("settingsVersion") == std::to_string(Graphite::kSettingsVersion));
}

TEST_CASE("A settings file from a newer version is refused and left as it was")
{
	Core::JsonFile file;
	const std::string newer = std::to_string(Graphite::kSettingsVersion + 1);
	file.Set("settingsVersion", newer);

	CHECK_FALSE(Graphite::StampSettingsVersion(file));
	CHECK(file.Get("settingsVersion") == newer);
}

// A hand-edited value that is not a number says nothing about which version wrote the file.
// Treating it as newer would lock the user out of every later change until they found it.
TEST_CASE("An unreadable settings version is replaced rather than refused")
{
	Core::JsonFile file;
	file.Set("settingsVersion", "soon");

	CHECK(Graphite::StampSettingsVersion(file));
	CHECK(file.Get("settingsVersion") == std::to_string(Graphite::kSettingsVersion));
}
