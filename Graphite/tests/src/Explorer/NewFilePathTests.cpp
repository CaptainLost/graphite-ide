#include <doctest/doctest.h>

#include "Explorer/NewFilePath.h"

using Graphite::NewFilePath;
using Graphite::ParseNewFilePath;

TEST_CASE("A plain name is a file with no folders to create")
{
	const std::optional<NewFilePath> path = ParseNewFilePath("Foo.cpp");

	REQUIRE(path.has_value());
	CHECK(path->Folders.empty());
	CHECK(path->File == "Foo.cpp");
}

TEST_CASE("A name with slashes asks for the folders on the way, in order")
{
	const std::optional<NewFilePath> path = ParseNewFilePath("util/text/Strings.cpp");

	REQUIRE(path.has_value());
	CHECK(path->Folders == std::vector<std::string>{"util", "text"});
	CHECK(path->File == "Strings.cpp");
}

TEST_CASE("A backslash separates folders like a slash")
{
	const std::optional<NewFilePath> path = ParseNewFilePath("util\\Strings.cpp");

	REQUIRE(path.has_value());
	CHECK(path->Folders == std::vector<std::string>{"util"});
}

TEST_CASE("A file without an extension is a valid name")
{
	CHECK(ParseNewFilePath("Makefile").has_value());
}

TEST_CASE("A name that names nothing, or climbs out of the folder, is refused")
{
	CHECK_FALSE(ParseNewFilePath("").has_value());
	CHECK_FALSE(ParseNewFilePath("   ").has_value());
	CHECK_FALSE(ParseNewFilePath("util/").has_value());
	CHECK_FALSE(ParseNewFilePath("/Foo.cpp").has_value());
	CHECK_FALSE(ParseNewFilePath("a//b.cpp").has_value());
	CHECK_FALSE(ParseNewFilePath("../Foo.cpp").has_value());
	CHECK_FALSE(ParseNewFilePath("a/./Foo.cpp").has_value());
}

TEST_CASE("A piece Windows would refuse refuses the whole name")
{
	CHECK_FALSE(ParseNewFilePath("a:b.txt").has_value());
	CHECK_FALSE(ParseNewFilePath("nul/Foo.cpp").has_value());
	CHECK_FALSE(ParseNewFilePath("notes.").has_value());
}
