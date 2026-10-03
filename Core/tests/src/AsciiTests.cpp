#include <doctest/doctest.h>

#include "Core/Base/Ascii.h"
#include "Core/Fs/FileSystem.h"

TEST_CASE("LowerAscii lowers the ASCII capitals and leaves every other byte alone")
{
	CHECK(Core::LowerAscii('A') == 'a');
	CHECK(Core::LowerAscii('Z') == 'z');
	CHECK(Core::LowerAscii('a') == 'a');
	CHECK(Core::LowerAscii('.') == '.');
	CHECK(Core::LowerAscii('\xc4') == '\xc4');
}

TEST_CASE("EqualsIgnoringCase compares ASCII without regard to case, and nothing else")
{
	CHECK(Core::EqualsIgnoringCase("Content-Length", "content-length"));
	CHECK_FALSE(Core::EqualsIgnoringCase("abc", "abcd"));
	CHECK_FALSE(Core::EqualsIgnoringCase("\xc4\x86", "\xc4\x87"));
	CHECK(Core::EqualsIgnoringCase("", ""));
}

TEST_CASE("LoweredExtension gives the extension with its dot, in lower case")
{
	CHECK(Core::LoweredExtension("D:/work/App.VCXPROJ") == ".vcxproj");
	CHECK(Core::LoweredExtension("Makefile").empty());
	CHECK(Core::LoweredExtension("archive.tar.GZ") == ".gz");
}
