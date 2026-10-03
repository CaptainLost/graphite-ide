#include <doctest/doctest.h>

#include "Workspace/DroppedPaths.h"

#include <algorithm>

namespace {

	// The drop callback hands over paths that exist on disk, so the tests answer for the
	// paths they name rather than reaching for a temporary directory: what is under test is
	// the routing, not the file system.
	Graphite::DirectoryPredicate DirectoriesAre(
		std::vector<std::filesystem::path> directories)
	{
		return [directories = std::move(directories)](const std::filesystem::path& path) {
			return std::find(directories.begin(), directories.end(), path) !=
				   directories.end();
		};
	}

	const Graphite::DirectoryPredicate kNothingIsADirectory = DirectoriesAre({});
}

TEST_CASE("An empty drop routes nowhere")
{
	const Graphite::DropRouting routing = Graphite::ClassifyDrop({}, kNothingIsADirectory);

	CHECK(routing.Target == Graphite::DropTarget::None);
	CHECK(routing.Files.empty());
}

TEST_CASE("A dropped file becomes a document to open")
{
	const Graphite::DropRouting routing =
		Graphite::ClassifyDrop({"D:/a/main.cpp"}, kNothingIsADirectory);

	CHECK(routing.Target == Graphite::DropTarget::None);
	REQUIRE(routing.Files.size() == 1);
	CHECK(routing.Files.front() == std::filesystem::path("D:/a/main.cpp"));
}

TEST_CASE("Every dropped file is opened, in the order they were dropped")
{
	const Graphite::DropRouting routing = Graphite::ClassifyDrop(
		{"D:/a/main.cpp", "D:/a/main.h"}, kNothingIsADirectory);

	REQUIRE(routing.Files.size() == 2);
	CHECK(routing.Files[0] == std::filesystem::path("D:/a/main.cpp"));
	CHECK(routing.Files[1] == std::filesystem::path("D:/a/main.h"));
}

TEST_CASE("A dropped directory becomes the workspace to open")
{
	const Graphite::DropRouting routing =
		Graphite::ClassifyDrop({"D:/a"}, DirectoriesAre({"D:/a"}));

	CHECK(routing.Target == Graphite::DropTarget::Folder);
	CHECK(routing.Root == std::filesystem::path("D:/a"));
	CHECK(routing.Files.empty());
}

TEST_CASE("A dropped solution becomes the solution to open")
{
	CHECK(Graphite::ClassifyDrop({"D:/a/App.sln"}, kNothingIsADirectory).Target ==
		  Graphite::DropTarget::Solution);
	CHECK(Graphite::ClassifyDrop({"D:/a/App.slnx"}, kNothingIsADirectory).Target ==
		  Graphite::DropTarget::Solution);
}

TEST_CASE("A solution is recognised whatever case its extension is written in")
{
	const Graphite::DropRouting routing =
		Graphite::ClassifyDrop({"D:/a/App.SLN"}, kNothingIsADirectory);

	CHECK(routing.Target == Graphite::DropTarget::Solution);
	CHECK(routing.Root == std::filesystem::path("D:/a/App.SLN"));
}

TEST_CASE("A folder dropped alongside files opens the folder and the files both")
{
	const Graphite::DropRouting routing = Graphite::ClassifyDrop(
		{"D:/a", "D:/a/main.cpp"}, DirectoriesAre({"D:/a"}));

	CHECK(routing.Target == Graphite::DropTarget::Folder);
	CHECK(routing.Root == std::filesystem::path("D:/a"));
	REQUIRE(routing.Files.size() == 1);
	CHECK(routing.Files.front() == std::filesystem::path("D:/a/main.cpp"));
}

TEST_CASE("Only the first of two dropped folders is opened")
{
	const Graphite::DropRouting routing =
		Graphite::ClassifyDrop({"D:/a", "D:/b"}, DirectoriesAre({"D:/a", "D:/b"}));

	CHECK(routing.Target == Graphite::DropTarget::Folder);
	CHECK(routing.Root == std::filesystem::path("D:/a"));
	CHECK(routing.Files.empty());
}

TEST_CASE("Whichever of a folder and a solution was dropped first wins")
{
	const Graphite::DropRouting routing = Graphite::ClassifyDrop(
		{"D:/a/App.sln", "D:/a"}, DirectoriesAre({"D:/a"}));

	CHECK(routing.Target == Graphite::DropTarget::Solution);
	CHECK(routing.Root == std::filesystem::path("D:/a/App.sln"));
	CHECK(routing.Files.empty());
}

TEST_CASE("A second solution is left alone rather than opened as a document")
{
	const Graphite::DropRouting routing = Graphite::ClassifyDrop(
		{"D:/a/App.sln", "D:/a/Other.sln"}, kNothingIsADirectory);

	CHECK(routing.Root == std::filesystem::path("D:/a/App.sln"));
	CHECK(routing.Files.empty());
}

TEST_CASE("A drop that opens nothing replaces no workspace")
{
	CHECK_FALSE(Graphite::ReplacesWorkspace({}, "D:/a"));
}

TEST_CASE("A folder dropped with nothing open replaces nothing")
{
	const Graphite::DropRouting routing{Graphite::DropTarget::Folder, "D:/a", {}};

	CHECK_FALSE(Graphite::ReplacesWorkspace(routing, {}));
}

TEST_CASE("A folder dropped over an open workspace replaces it")
{
	const Graphite::DropRouting routing{Graphite::DropTarget::Folder, "D:/b", {}};

	CHECK(Graphite::ReplacesWorkspace(routing, "D:/a"));
}

TEST_CASE("Dropping the folder already open replaces nothing")
{
	const Graphite::DropRouting routing{Graphite::DropTarget::Folder, "D:/a", {}};

	CHECK_FALSE(Graphite::ReplacesWorkspace(routing, "D:/a"));
}

TEST_CASE("A solution inside the open workspace replaces nothing")
{
	const Graphite::DropRouting routing{
		Graphite::DropTarget::Solution, "D:/a/sub/App.sln", {}};

	CHECK_FALSE(Graphite::ReplacesWorkspace(routing, "D:/a"));
}

TEST_CASE("A solution outside the open workspace replaces it, since opening it moves the root")
{
	const Graphite::DropRouting routing{Graphite::DropTarget::Solution, "D:/b/App.sln", {}};

	CHECK(Graphite::ReplacesWorkspace(routing, "D:/a"));
}

TEST_CASE("A neighbour whose name merely starts with the root is outside it")
{
	const Graphite::DropRouting routing{Graphite::DropTarget::Folder, "D:/abc", {}};

	CHECK(Graphite::ReplacesWorkspace(routing, "D:/ab"));
}
