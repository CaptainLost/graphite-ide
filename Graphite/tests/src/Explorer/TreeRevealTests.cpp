#include <doctest/doctest.h>

#include "Explorer/TreeReveal.h"
#include "Solution/SolutionModel.h"
#include "TempDirectory.h"

#include "Core/FileSystem/FileTree.h"

#include <filesystem>
#include <vector>

using Graphite::ExpandToward;
using Graphite::HoldsFile;

namespace {

	const Core::FileNode* ChildNamed(const Core::FileNode& node, const char* name)
	{
		for (const Core::FileNode& child : node.Children)
		{
			if (child.Name == name)
			{
				return &child;
			}
		}

		return nullptr;
	}
}

TEST_CASE("ExpandToward opens every folder from the root down to the file, root first")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("src/Core/App/EntryPoint.h");
	temp.MakeFile("src/other.cpp");

	Core::FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()));

	const std::vector<std::filesystem::path> opened = ExpandToward(tree, file);

	REQUIRE(opened.size() == 4);
	CHECK(opened[0] == temp.GetPath());
	CHECK(opened[1] == temp.GetPath() / "src");
	CHECK(opened[2] == temp.GetPath() / "src" / "Core");
	CHECK(opened[3] == temp.GetPath() / "src" / "Core" / "App");

	const Core::FileNode& root = tree.GetRoot();
	CHECK(root.Expanded);

	const Core::FileNode* sourceFolder = ChildNamed(root, "src");
	REQUIRE(sourceFolder != nullptr);
	CHECK(sourceFolder->Expanded);

	const Core::FileNode* core = ChildNamed(*sourceFolder, "Core");
	REQUIRE(core != nullptr);

	const Core::FileNode* app = ChildNamed(*core, "App");
	REQUIRE(app != nullptr);
	CHECK(app->Expanded);
	CHECK(ChildNamed(*app, "EntryPoint.h") != nullptr);
}

TEST_CASE("ExpandToward accepts the file spelled with forward slashes")
{
	TempDirectory temp;
	temp.MakeFile("a/b.h");

	Core::FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()));

	const std::filesystem::path forward{temp.GetPath().generic_wstring() + L"/a/b.h"};

	CHECK(ExpandToward(tree, forward).size() == 2);
}

TEST_CASE("ExpandToward leaves the tree alone for a file outside the root")
{
	TempDirectory temp;
	temp.MakeFile("inside/a.h");
	const std::filesystem::path outside = temp.MakeFile("elsewhere/b.h");

	Core::FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath() / "inside"));

	CHECK(ExpandToward(tree, outside).empty());
	CHECK_FALSE(tree.GetRoot().Expanded);
}

TEST_CASE("ExpandToward stops at the first folder that is not there")
{
	TempDirectory temp;
	temp.MakeFile("a/x.h");

	Core::FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()));

	const std::vector<std::filesystem::path> opened =
		ExpandToward(tree, temp.GetPath() / "a" / "gone" / "y.h");

	REQUIRE(opened.size() == 2);
	CHECK(opened[1] == temp.GetPath() / "a");
}

TEST_CASE("HoldsFile finds a file at any depth of a solution and nowhere else")
{
	const std::filesystem::path wanted = "D:/repo/Core/src/EntryPoint.h";

	Graphite::ContentFolder inner;
	inner.Name = "src";
	inner.Files.push_back({"EntryPoint.h", wanted});

	Graphite::Project core;
	core.DisplayName = "Core";
	core.ContentRead = true;
	core.Content.Root.Folders.push_back(inner);

	Graphite::Project app;
	app.DisplayName = "App";
	app.ContentRead = true;
	app.Content.Root.Files.push_back({"Main.cpp", "D:/repo/App/Main.cpp"});

	Graphite::Folder engine;
	engine.Name = "Engine";
	engine.Projects.push_back(core);

	Graphite::Folder root;
	root.Folders.push_back(engine);
	root.Projects.push_back(app);

	CHECK(HoldsFile(root, wanted));
	CHECK(HoldsFile(engine, wanted));
	CHECK(HoldsFile(core, wanted));
	CHECK(HoldsFile(core.Content.Root, wanted));
	CHECK_FALSE(HoldsFile(app, wanted));
	CHECK_FALSE(HoldsFile(root, "D:/repo/Core/src/Other.h"));
}
