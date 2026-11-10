#include <doctest/doctest.h>

#include "Core/FileSystem/WslSharePath.h"

#include "Core/Base/PlatformDetection.h"
#include "Core/FileSystem/FileSystem.h"

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

// The host's spelling of a place in WSL: Windows only, as only the host meets these paths.
#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("Every spelling of a place in WSL comes back as the one the host uses")
{
	CHECK(Core::CanonicalWslSharePath(L"\\\\wsl$\\Ubuntu\\home\\me\\").native() == L"\\\\wsl.localhost\\Ubuntu\\home\\me");
	CHECK(Core::CanonicalWslSharePath(L"//WSL.LOCALHOST/Ubuntu-22.04/x").native() == L"\\\\wsl.localhost\\Ubuntu-22.04\\x");
	CHECK(Core::CanonicalWslSharePath(L"\\\\?\\UNC\\wsl$\\Ubuntu").native() == L"\\\\wsl.localhost\\Ubuntu\\");
}

TEST_CASE("A path that is not in WSL is left as it is")
{
	CHECK(Core::CanonicalWslSharePath(L"D:\\x").native() == L"D:\\x");
	CHECK(Core::CanonicalWslSharePath(L"\\\\nas\\share\\x").native() == L"\\\\nas\\share\\x");
}

TEST_CASE("A path is found below its folder whatever spelling of WSL each was written in")
{
	CHECK(Core::RelativeTo(L"\\\\wsl$\\Ubuntu\\home\\me\\p\\a.cpp", L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p") ==
		  std::filesystem::path{L"a.cpp"});
	CHECK(Core::RelativeTo(L"\\\\?\\UNC\\wsl.localhost\\Ubuntu\\home\\me\\p\\src\\deep\\f.cpp", L"\\\\wsl$\\Ubuntu\\home\\me\\p") ==
		  std::filesystem::path{L"src\\deep\\f.cpp"});
}

TEST_CASE("A distro's name is one place in any case, as WSL takes it, while the names inside keep theirs")
{
	CHECK(Core::SamePath(L"\\\\wsl.localhost\\ubuntu\\a", L"\\\\wsl.localhost\\Ubuntu\\a"));
	CHECK_FALSE(Core::SamePath(L"\\\\wsl.localhost\\ubuntu\\a", L"\\\\wsl.localhost\\Ubuntu\\A"));
	CHECK(Core::RelativeTo(L"\\\\wsl$\\UBUNTU\\home\\x.cpp", L"\\\\wsl.localhost\\Ubuntu\\home") == std::filesystem::path{L"x.cpp"});
}

TEST_CASE("Paths in WSL keep their case as keys, and every spelling of one place is one key")
{
	CHECK_FALSE(Core::SamePath(L"\\\\wsl.localhost\\Ubuntu\\a\\Makefile", L"\\\\wsl.localhost\\Ubuntu\\a\\makefile"));
	CHECK(Core::SamePath(L"\\\\wsl$\\Ubuntu\\a\\Makefile", L"\\\\WSL.localhost\\Ubuntu\\a\\Makefile\\"));
	CHECK(Core::SamePath("C:/a/Makefile", "C:/a/makefile"));
}
#endif
