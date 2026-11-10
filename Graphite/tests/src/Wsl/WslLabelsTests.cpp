#include <doctest/doctest.h>

#include "Wsl/WslLabels.h"

#include <filesystem>
#include <optional>
#include <string>

TEST_CASE("A folder inside WSL is listed by its distro and its Linux path")
{
	CHECK(Graphite::FolderDisplayName(L"\\\\wsl.localhost\\Ubuntu-22.04\\home\\me\\proj") == "Ubuntu-22.04: /home/me/proj");
	CHECK(Graphite::FolderDisplayName(L"\\\\wsl.localhost\\Ubuntu\\") == "Ubuntu: /");
}

TEST_CASE("Any other folder is listed as its path")
{
	CHECK(Graphite::FolderDisplayName(L"D:\\Repos\\IDE") == "D:\\Repos\\IDE");
}

TEST_CASE("A folder inside WSL is titled by its distro and its name, a local one not at all")
{
	CHECK(Graphite::WslFolderTitle(L"\\\\wsl.localhost\\Ubuntu\\home\\me\\proj") == std::optional<std::string>{"Ubuntu: proj"});
	CHECK(Graphite::WslFolderTitle(L"\\\\wsl.localhost\\Ubuntu\\") == std::optional<std::string>{"Ubuntu: /"});
	CHECK_FALSE(Graphite::WslFolderTitle(L"D:\\Repos\\IDE").has_value());
}
