#include <doctest/doctest.h>

#include "Workspace/DroppedPaths.h"

#include <algorithm>

namespace {

	Graphite::DirectoryPredicate DirectoriesAre(
		std::vector<std::filesystem::path> directories)
	{
		return [directories = std::move(directories)](const std::filesystem::path& path) {
			return std::find(directories.begin(), directories.end(), path) !=
				   directories.end();
		};
	}

	const std::vector<std::filesystem::path> kNoDirectories;
	const std::vector<std::filesystem::path> kNothingDropped;
	const std::filesystem::path kNothingOpen;
	const Graphite::DropRouting kEmptyRouting;
	const Graphite::DirectoryPredicate kNothingIsADirectory = DirectoriesAre(kNoDirectories);
}

TEST_CASE("An empty drop routes nowhere")
{
	const Graphite::DropRouting routing = Graphite::ClassifyDrop(kNothingDropped, kNothingIsADirectory);

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
	CHECK_FALSE(Graphite::ReplacesWorkspace(kEmptyRouting, "D:/a"));
}

TEST_CASE("A folder dropped with nothing open replaces nothing")
{
	const Graphite::DropRouting routing{.Target = Graphite::DropTarget::Folder, .Root = "D:/a"};

	CHECK_FALSE(Graphite::ReplacesWorkspace(routing, kNothingOpen));
}

TEST_CASE("A folder dropped over an open workspace replaces it")
{
	const Graphite::DropRouting routing{.Target = Graphite::DropTarget::Folder, .Root = "D:/b"};

	CHECK(Graphite::ReplacesWorkspace(routing, "D:/a"));
}

TEST_CASE("Dropping the folder already open replaces nothing")
{
	const Graphite::DropRouting routing{.Target = Graphite::DropTarget::Folder, .Root = "D:/a"};

	CHECK_FALSE(Graphite::ReplacesWorkspace(routing, "D:/a"));
}

TEST_CASE("A solution inside the open workspace replaces nothing")
{
	const Graphite::DropRouting routing{.Target = Graphite::DropTarget::Solution, .Root = "D:/a/sub/App.sln"};

	CHECK_FALSE(Graphite::ReplacesWorkspace(routing, "D:/a"));
}

TEST_CASE("A solution outside the open workspace replaces it, since opening it moves the root")
{
	const Graphite::DropRouting routing{.Target = Graphite::DropTarget::Solution, .Root = "D:/b/App.sln"};

	CHECK(Graphite::ReplacesWorkspace(routing, "D:/a"));
}

TEST_CASE("A neighbour whose name merely starts with the root is outside it")
{
	const Graphite::DropRouting routing{.Target = Graphite::DropTarget::Folder, .Root = "D:/abc"};

	CHECK(Graphite::ReplacesWorkspace(routing, "D:/ab"));
}
