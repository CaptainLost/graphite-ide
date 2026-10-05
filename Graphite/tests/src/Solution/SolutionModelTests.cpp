#include <doctest/doctest.h>

#include "Solution/SolutionModel.h"

using namespace Graphite;

TEST_CASE("EnsureFolderPath reuses folders by exact name and appends the missing ones in order")
{
	Folder root;

	CHECK(EnsureFolderPath(root, {"src", "core"}).Name == "core");
	EnsureFolderPath(root, {"src", "ui"});
	REQUIRE(root.Folders.size() == 1);
	CHECK(root.Folders[0].Folders.size() == 2);
	CHECK(root.Folders[0].Folders[1].Name == "ui");

	EnsureFolderPath(root, {"Src"});
	CHECK(root.Folders.size() == 2);

	const std::vector<std::string> noFolderNames;
	CHECK(&EnsureFolderPath(root, noFolderNames) == &root);

	ContentFolder content;
	CHECK(EnsureFolderPath(content, {"x"}).Name == "x");
}
