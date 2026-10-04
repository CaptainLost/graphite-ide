#include <doctest/doctest.h>

#include "UI/RelativePaths.h"

#include <filesystem>

using Graphite::RelativiseUnder;

namespace {
	const std::filesystem::path kRoot = "D:/Repos/IDE";
}

TEST_CASE("A path inside the root is written relative to it")
{
	CHECK(RelativiseUnder("cannot open D:/Repos/IDE/.claude/x.jsonl: denied", kRoot) ==
		  "cannot open .claude/x.jsonl: denied");
}

TEST_CASE("Either separator matches either, in the message and in the root")
{
	CHECK(RelativiseUnder("at D:\\Repos\\IDE\\src\\a.cpp", kRoot) == "at src\\a.cpp");
	CHECK(RelativiseUnder("at D:/Repos\\IDE/src/a.cpp", std::filesystem::path("D:\\Repos\\IDE")) == "at src/a.cpp");
}

TEST_CASE("The drive letter matches in either case, the rest of the path exactly")
{
	CHECK(RelativiseUnder("d:/Repos/IDE/a", kRoot) == "a");
	CHECK(RelativiseUnder("D:/Repos/IDE/a", std::filesystem::path("d:/Repos/IDE")) == "a");
	CHECK(RelativiseUnder("D:/repos/ide/a", kRoot) == "D:/repos/ide/a");
}

TEST_CASE("A root given with a trailing separator works the same")
{
	CHECK(RelativiseUnder("D:/Repos/IDE/a/b", std::filesystem::path("D:/Repos/IDE/")) == "a/b");
}

TEST_CASE("Only whole paths strictly inside the root change")
{
	CHECK(RelativiseUnder("xD:/Repos/IDE/a", kRoot) == "xD:/Repos/IDE/a");
	CHECK(RelativiseUnder("D:/Repos/IDE2/a", kRoot) == "D:/Repos/IDE2/a");
	CHECK(RelativiseUnder("in D:/Repos/IDE now", kRoot) == "in D:/Repos/IDE now");
	CHECK(RelativiseUnder("in D:/Repos/IDE/ now", kRoot) == "in D:/Repos/IDE/ now");
	CHECK(RelativiseUnder("C:/Other/a and D:/Repos/a", kRoot) == "C:/Other/a and D:/Repos/a");
}

TEST_CASE("A quoted path and every path in the message change")
{
	CHECK(RelativiseUnder("'D:/Repos/IDE/a' and \"D:/Repos/IDE/b\" and (D:/Repos/IDE/c)", kRoot) ==
		  "'a' and \"b\" and (c)");
	CHECK(RelativiseUnder("D:/Repos/IDE/a\nD:/Repos/IDE/b", kRoot) == "a\nb");
}

TEST_CASE("An empty root changes nothing")
{
	CHECK(RelativiseUnder("D:/Repos/IDE/a", std::filesystem::path()) == "D:/Repos/IDE/a");
}

TEST_CASE("A root of only a separator changes nothing")
{
	CHECK(RelativiseUnder("open /a/b and \\c\\d", std::filesystem::path("/")) == "open /a/b and \\c\\d");
	CHECK(RelativiseUnder("open /a/b", std::filesystem::path("\\")) == "open /a/b");
}

TEST_CASE("A root with letters outside ASCII matches its UTF-8 spelling in the message")
{
	const std::filesystem::path root(u8"D:/Projekty/Za\u017C\u00F3\u0142\u0107");

	CHECK(RelativiseUnder("at D:/Projekty/Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87/src/a.cpp", root) == "at src/a.cpp");
	CHECK(RelativiseUnder("at D:\\Projekty\\Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87\\a", root) == "at a");
	CHECK(RelativiseUnder("at D:/Projekty/Zazolc/a", root) == "at D:/Projekty/Zazolc/a");
}

TEST_CASE("A message with no path comes back as it was")
{
	CHECK(RelativiseUnder("", kRoot).empty());
	CHECK(RelativiseUnder("nothing to see", kRoot) == "nothing to see");
}
