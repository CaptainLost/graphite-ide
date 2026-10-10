#include <doctest/doctest.h>

#include "Core/FileSystem/RelativePath.h"

#include <string_view>

TEST_CASE("A relative path that stays below its base is safe")
{
	CHECK(Core::IsSafeRelativePath("clangd_23.1.0/bin/clangd.exe"));
	CHECK(Core::IsSafeRelativePath("tools/net10.0/any/CSharpLanguageServer.dll"));
	CHECK(Core::IsSafeRelativePath("tools\\net10.0\\win-x64\\roslyn-language-server.exe"));
	CHECK(Core::IsSafeRelativePath("[Content_Types].xml"));
	CHECK(Core::IsSafeRelativePath("_rels/.rels"));
	CHECK(Core::IsSafeRelativePath(".signature.p7s"));
	CHECK(Core::IsSafeRelativePath("lib/clang/23/include/"));
	CHECK(Core::IsSafeRelativePath("CONSOLE.txt"));
	CHECK(Core::IsSafeRelativePath("COM10"));
}

TEST_CASE("A path that is empty or rooted or names a drive is refused")
{
	CHECK_FALSE(Core::IsSafeRelativePath(""));
	CHECK_FALSE(Core::IsSafeRelativePath("/etc/passwd"));
	CHECK_FALSE(Core::IsSafeRelativePath("\\Windows\\evil.dll"));
	CHECK_FALSE(Core::IsSafeRelativePath("//server/share/evil.dll"));
	CHECK_FALSE(Core::IsSafeRelativePath("C:/evil.dll"));
	CHECK_FALSE(Core::IsSafeRelativePath("c:evil.dll"));
}

TEST_CASE("A path that climbs or that Windows would rename is refused")
{
	CHECK_FALSE(Core::IsSafeRelativePath(".."));
	CHECK_FALSE(Core::IsSafeRelativePath("../evil.dll"));
	CHECK_FALSE(Core::IsSafeRelativePath("bin/../../evil.dll"));
	CHECK_FALSE(Core::IsSafeRelativePath("bin\\..\\..\\evil.dll"));
	CHECK_FALSE(Core::IsSafeRelativePath("./evil.dll"));
	CHECK_FALSE(Core::IsSafeRelativePath("bin/.../evil.dll"));
	CHECK_FALSE(Core::IsSafeRelativePath("bin./evil.dll"));
	CHECK_FALSE(Core::IsSafeRelativePath("bin /evil.dll"));
}

TEST_CASE("A path holding a stream or a device name or a forbidden character is refused")
{
	CHECK_FALSE(Core::IsSafeRelativePath("tool.exe:hidden"));
	CHECK_FALSE(Core::IsSafeRelativePath("CON"));
	CHECK_FALSE(Core::IsSafeRelativePath("bin/nul.txt"));
	CHECK_FALSE(Core::IsSafeRelativePath("com1.log"));
	CHECK_FALSE(Core::IsSafeRelativePath("Lpt9"));
	CHECK_FALSE(Core::IsSafeRelativePath("a<b"));
	CHECK_FALSE(Core::IsSafeRelativePath("a|b"));
	CHECK_FALSE(Core::IsSafeRelativePath("a?b"));
	CHECK_FALSE(Core::IsSafeRelativePath("a*b"));
	CHECK_FALSE(Core::IsSafeRelativePath("a\"b"));
	CHECK_FALSE(Core::IsSafeRelativePath(std::string_view{"a\x01" "b"}));
}
