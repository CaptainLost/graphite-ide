#include <doctest/doctest.h>

#include "Solution/SolutionEditor.h"

#include <string>

// What a UUID is and what it guarantees is covered by UuidTests. What is left here is the
// one thing this function adds: the braced spelling both solution formats use.

TEST_CASE("A generated identifier has the shape both solution formats expect")
{
	const std::string guid = Graphite::NewGuid();

	REQUIRE(guid.size() == 38);
	CHECK(guid.front() == '{');
	CHECK(guid.back() == '}');
	CHECK(guid[9] == '-');
	CHECK(guid[14] == '-');
	CHECK(guid[19] == '-');
	CHECK(guid[24] == '-');
}

TEST_CASE("An identifier is never empty")
{
	// The previous implementation returned an empty string wherever CoCreateGuid was not
	// available, which made every caller carry a check for a case it could not handle —
	// and made adding a project impossible on any other system, for no reason to do with
	// adding projects.
	CHECK_FALSE(Graphite::NewGuid().empty());
}

TEST_CASE("A new folder keeps the base name while it is free")
{
	Graphite::Folder parent;
	parent.Folders.push_back(Graphite::Folder{"Libraries", {}, {}});

	CHECK(Graphite::UniqueFolderName(parent, "New Folder") == "New Folder");
	CHECK(Graphite::UniqueFolderName(Graphite::Folder{}, "New Folder") == "New Folder");
}

TEST_CASE("A new folder takes the first free number when the base name is taken")
{
	Graphite::Folder parent;
	parent.Folders.push_back(Graphite::Folder{"New Folder", {}, {}});

	CHECK(Graphite::UniqueFolderName(parent, "New Folder") == "New Folder 2");

	parent.Folders.push_back(Graphite::Folder{"New Folder 2", {}, {}});
	CHECK(Graphite::UniqueFolderName(parent, "New Folder") == "New Folder 3");

	// A gap is filled, not skipped over.
	parent.Folders.erase(parent.Folders.begin());
	CHECK(Graphite::UniqueFolderName(parent, "New Folder") == "New Folder");
}

TEST_CASE("Folder names are compared without regard to case")
{
	Graphite::Folder parent;
	parent.Folders.push_back(Graphite::Folder{"new folder", {}, {}});

	CHECK(Graphite::UniqueFolderName(parent, "New Folder") == "New Folder 2");
}
