#include <doctest/doctest.h>

#include "Core/FileSystem/FileName.h"

#include <string_view>

using Core::IsValidFileName;

TEST_CASE("IsValidFileName accepts ordinary names")
{
	CHECK(IsValidFileName("main.cpp"));
	CHECK(IsValidFileName(".gitignore"));
	CHECK(IsValidFileName("a name with spaces.txt"));
	CHECK(IsValidFileName("CONSOLE.txt"));
	CHECK(IsValidFileName("COM10.txt"));
}

TEST_CASE("IsValidFileName rejects an empty or blank name")
{
	CHECK_FALSE(IsValidFileName(""));
	CHECK_FALSE(IsValidFileName("   "));
}

TEST_CASE("IsValidFileName rejects characters the filesystem forbids")
{
	CHECK_FALSE(IsValidFileName("a/b"));
	CHECK_FALSE(IsValidFileName("a\\b"));
	CHECK_FALSE(IsValidFileName("a:b"));
	CHECK_FALSE(IsValidFileName("a*b"));
	CHECK_FALSE(IsValidFileName("a?b"));
	CHECK_FALSE(IsValidFileName("a\"b"));
	CHECK_FALSE(IsValidFileName("a<b"));
	CHECK_FALSE(IsValidFileName("a>b"));
	CHECK_FALSE(IsValidFileName("a|b"));
}

TEST_CASE("IsValidFileName rejects a trailing dot or space")
{
	CHECK_FALSE(IsValidFileName("name."));
	CHECK_FALSE(IsValidFileName("name "));
}

TEST_CASE("IsValidFileName rejects reserved device names with any number of extensions")
{
	CHECK_FALSE(IsValidFileName("CON"));
	CHECK_FALSE(IsValidFileName("con"));
	CHECK_FALSE(IsValidFileName("NUL.txt"));
	CHECK_FALSE(IsValidFileName("PRN"));
	CHECK_FALSE(IsValidFileName("AUX"));
	CHECK_FALSE(IsValidFileName("COM1"));
	CHECK_FALSE(IsValidFileName("COM9.cpp"));
	CHECK_FALSE(IsValidFileName("LPT1"));
	CHECK_FALSE(IsValidFileName("lpt9.log"));
	CHECK_FALSE(IsValidFileName("NUL.tar.gz"));
	CHECK_FALSE(IsValidFileName("com1.a.b"));
}

TEST_CASE("IsValidFileName rejects the device names Windows reserves beyond the numbered ports one to nine")
{
	CHECK_FALSE(IsValidFileName("COM0"));
	CHECK_FALSE(IsValidFileName("lpt0.txt"));
	CHECK_FALSE(IsValidFileName("COM\xC2\xB9"));
	CHECK_FALSE(IsValidFileName("lpt\xC2\xB3.log"));
	CHECK_FALSE(IsValidFileName("CONIN$"));
	CHECK_FALSE(IsValidFileName("conout$.txt"));
}

TEST_CASE("IsValidFileName rejects a device name followed by spaces before its extension, which Windows ignores")
{
	CHECK_FALSE(IsValidFileName("nul .txt"));
	CHECK_FALSE(IsValidFileName("CON  .log"));
}

TEST_CASE("IsValidFileName rejects control characters")
{
	CHECK_FALSE(IsValidFileName("a\tb"));
	CHECK_FALSE(IsValidFileName("line\nbreak"));
	CHECK_FALSE(IsValidFileName(std::string_view{"a\x01" "b"}));
}
