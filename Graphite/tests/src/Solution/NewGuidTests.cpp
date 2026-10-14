#include <doctest/doctest.h>

#include "Solution/SolutionEditor.h"

#include <cstddef>
#include <string>

namespace {
	constexpr std::size_t kBracedGuidLength = 38;
	constexpr std::size_t kFirstDashPosition = 9;
	constexpr std::size_t kSecondDashPosition = 14;
	constexpr std::size_t kThirdDashPosition = 19;
	constexpr std::size_t kFourthDashPosition = 24;
}

TEST_CASE("A generated identifier is spelled in braces, the shape both solution formats expect")
{
	const std::string guid = Graphite::NewGuid();

	REQUIRE(guid.size() == kBracedGuidLength);
	CHECK(guid.front() == '{');
	CHECK(guid.back() == '}');
	CHECK(guid[kFirstDashPosition] == '-');
	CHECK(guid[kSecondDashPosition] == '-');
	CHECK(guid[kThirdDashPosition] == '-');
	CHECK(guid[kFourthDashPosition] == '-');
}

TEST_CASE("An identifier is never empty, on any system")
{
	CHECK_FALSE(Graphite::NewGuid().empty());
}

TEST_CASE("A new folder keeps the base name while it is free")
{
	Graphite::Folder parent;
	parent.Folders.push_back(Graphite::Folder{.Name = "Libraries"});

	CHECK(Graphite::UniqueFolderName(parent, "New Folder") == "New Folder");
	CHECK(Graphite::UniqueFolderName(Graphite::Folder{}, "New Folder") == "New Folder");
}

TEST_CASE("A new folder takes the first free number when the base name is taken, filling a gap rather than skipping over it")
{
	Graphite::Folder parent;
	parent.Folders.push_back(Graphite::Folder{.Name = "New Folder"});

	CHECK(Graphite::UniqueFolderName(parent, "New Folder") == "New Folder 2");

	parent.Folders.push_back(Graphite::Folder{.Name = "New Folder 2"});
	CHECK(Graphite::UniqueFolderName(parent, "New Folder") == "New Folder 3");

	parent.Folders.erase(parent.Folders.begin());
	CHECK(Graphite::UniqueFolderName(parent, "New Folder") == "New Folder");
}

TEST_CASE("Folder names are compared without regard to case")
{
	Graphite::Folder parent;
	parent.Folders.push_back(Graphite::Folder{.Name = "new folder"});

	CHECK(Graphite::UniqueFolderName(parent, "New Folder") == "New Folder 2");
}
