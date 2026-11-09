#include <doctest/doctest.h>

#include "Core/FileSystem/WslSharePath.h"

using Core::ParseWslSharePath;
using Core::WslSharePath;

TEST_CASE("A path under wsl.localhost names its distro and its Linux path")
{
	CHECK(ParseWslSharePath(L"\\\\wsl.localhost\\Ubuntu\\home\\me\\proj") == WslSharePath{"Ubuntu", "/home/me/proj"});
}

TEST_CASE("The wsl$ spelling, any case and forward slashes name the same place")
{
	CHECK(ParseWslSharePath(L"\\\\wsl$\\Ubuntu\\home") == WslSharePath{"Ubuntu", "/home"});
	CHECK(ParseWslSharePath(L"\\\\WSL.LOCALHOST\\Ubuntu\\home") == WslSharePath{"Ubuntu", "/home"});
	CHECK(ParseWslSharePath(L"//wsl.localhost/Ubuntu/home/") == WslSharePath{"Ubuntu", "/home"});
}

TEST_CASE("The long-path UNC prefix is understood")
{
	CHECK(ParseWslSharePath(L"\\\\?\\UNC\\wsl.localhost\\Ubuntu\\x") == WslSharePath{"Ubuntu", "/x"});
}

TEST_CASE("A distro's root is the Linux root, and the distro name is kept verbatim")
{
	CHECK(ParseWslSharePath(L"\\\\wsl.localhost\\Ubuntu-22.04") == WslSharePath{"Ubuntu-22.04", "/"});
	CHECK(ParseWslSharePath(L"\\\\wsl.localhost\\Ubuntu-22.04\\") == WslSharePath{"Ubuntu-22.04", "/"});
}

TEST_CASE("A non-ASCII segment reaches the Linux path as UTF-8")
{
	const auto parsed = ParseWslSharePath(L"\\\\wsl.localhost\\Ubuntu\\home\\za\u017C\u00F3\u0142\u0107");

	REQUIRE(parsed.has_value());
	CHECK(parsed->LinuxPath == "/home/za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87");
}

TEST_CASE("Other shares, local paths and a bare host are not WSL")
{
	CHECK_FALSE(ParseWslSharePath(L"\\\\nas\\share\\x").has_value());
	CHECK_FALSE(ParseWslSharePath(L"D:\\wsl.localhost\\Ubuntu").has_value());
	CHECK_FALSE(ParseWslSharePath(L"\\\\wsl.localhost").has_value());
	CHECK_FALSE(ParseWslSharePath(L"\\\\wsl.localhost\\").has_value());
}
