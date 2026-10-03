#include <doctest/doctest.h>

#include "Core/Fs/FileSystem.h"
#include "Core/Fs/FileTree.h"

#include "TempDirectory.h"

#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

using Core::FileNode;
using Core::FileTree;

TEST_CASE("SetRoot creates a collapsed, unloaded root node")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	FileTree tree;
	const auto result = tree.SetRoot(temp.GetPath());

	REQUIRE(result.HasValue());
	CHECK(tree.HasRoot());
	CHECK(tree.GetRoot().IsDirectory);
	CHECK(tree.GetRoot().Path == temp.GetPath());
	CHECK_FALSE(tree.GetRoot().Expanded);
	CHECK_FALSE(tree.GetRoot().Loaded);
	CHECK(tree.GetRoot().Children.empty());
}

TEST_CASE("SetRoot rejects a path that is not a directory")
{
	TempDirectory temp;
	const auto file = temp.MakeFile("a.txt", "x");

	FileTree tree;
	const auto result = tree.SetRoot(file);

	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK_FALSE(tree.HasRoot());
}

TEST_CASE("Expand loads children on first use and marks the node loaded")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");
	temp.MakeDir("sub");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());

	const auto result = tree.Expand(tree.GetRoot());

	REQUIRE(result.HasValue());
	CHECK(tree.GetRoot().Expanded);
	CHECK(tree.GetRoot().Loaded);
	CHECK(tree.GetRoot().Children.size() == 2);
}

TEST_CASE("Expand sorts directories before files, case-insensitively within each group")
{
	TempDirectory temp;
	temp.MakeFile("Zebra.txt", "x");
	temp.MakeFile("apple.txt", "x");
	temp.MakeDir("Yankee");
	temp.MakeDir("bravo");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	const auto& children = tree.GetRoot().Children;
	REQUIRE(children.size() == 4);
	CHECK(children[0].Name == "bravo");
	CHECK(children[1].Name == "Yankee");
	CHECK(children[2].Name == "apple.txt");
	CHECK(children[3].Name == "Zebra.txt");
}

TEST_CASE("Expand on an already loaded node does not re-read the disk")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	// A file created after the first expansion must not appear until an explicit refresh.
	temp.MakeFile("b.txt", "x");
	tree.Collapse(tree.GetRoot());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	CHECK(tree.GetRoot().Children.size() == 1);
}

TEST_CASE("Collapse clears the expanded flag but keeps loaded children")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	tree.Collapse(tree.GetRoot());

	CHECK_FALSE(tree.GetRoot().Expanded);
	CHECK(tree.GetRoot().Loaded);
	CHECK(tree.GetRoot().Children.size() == 1);
}

TEST_CASE("Expanding an unreadable directory marks it inaccessible instead of failing")
{
	TempDirectory temp;
	const auto missing = temp.GetPath() / "vanished";

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	// Fabricate a node pointing at a directory that does not exist.
	FileNode ghost;
	ghost.Path = missing;
	ghost.Name = "vanished";
	ghost.IsDirectory = true;

	const auto result = tree.Expand(ghost);

	REQUIRE(result.HasValue());
	CHECK_FALSE(ghost.Accessible);
	CHECK(ghost.Loaded);
	CHECK(ghost.Children.empty());
}

TEST_CASE("Refresh picks up a file created after the first expansion")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.GetRoot().Children.size() == 1);

	temp.MakeFile("b.txt", "x");
	tree.Refresh(tree.GetRoot());

	CHECK(tree.GetRoot().Children.size() == 2);
}

TEST_CASE("Refresh drops nodes that disappeared from disk")
{
	TempDirectory temp;
	const auto doomed = temp.MakeFile("gone.txt", "x");
	temp.MakeFile("stays.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	std::error_code code;
	std::filesystem::remove(doomed, code);
	tree.Refresh(tree.GetRoot());

	REQUIRE(tree.GetRoot().Children.size() == 1);
	CHECK(tree.GetRoot().Children[0].Name == "stays.txt");
}

TEST_CASE("Refresh keeps an expanded subdirectory expanded and loaded")
{
	TempDirectory temp;
	temp.MakeFile("sub/inner.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.GetRoot().Children.size() == 1);
	REQUIRE(tree.Expand(tree.GetRoot().Children[0]).HasValue());
	REQUIRE(tree.GetRoot().Children[0].Children.size() == 1);

	temp.MakeFile("second.txt", "x");
	tree.Refresh(tree.GetRoot());

	REQUIRE(tree.GetRoot().Children.size() == 2);
	const FileNode& sub = tree.GetRoot().Children[0];
	CHECK(sub.Name == "sub");
	CHECK(sub.Expanded);
	CHECK(sub.Loaded);
	REQUIRE(sub.Children.size() == 1);
	CHECK(sub.Children[0].Name == "inner.txt");
}

TEST_CASE("Refresh re-reads an expanded subdirectory so its own changes appear")
{
	TempDirectory temp;
	temp.MakeFile("sub/one.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot().Children[0]).HasValue());

	temp.MakeFile("sub/two.txt", "x");
	tree.Refresh(tree.GetRoot());

	CHECK(tree.GetRoot().Children[0].Children.size() == 2);
}

TEST_CASE("Refresh leaves a collapsed, never-opened subdirectory unloaded")
{
	TempDirectory temp;
	temp.MakeFile("sub/inner.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	tree.Refresh(tree.GetRoot());

	const FileNode& sub = tree.GetRoot().Children[0];
	CHECK_FALSE(sub.Expanded);
	CHECK_FALSE(sub.Loaded);
	CHECK(sub.Children.empty());
}

TEST_CASE("Refresh on a node that was never loaded does nothing")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());

	tree.Refresh(tree.GetRoot());

	CHECK_FALSE(tree.GetRoot().Loaded);
	CHECK(tree.GetRoot().Children.empty());
}

TEST_CASE("Without a query nothing is ignored")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	REQUIRE(tree.GetRoot().Children.size() == 1);
	CHECK_FALSE(tree.GetRoot().Children[0].Ignored);
}

TEST_CASE("Expand marks the children the query rejects")
{
	TempDirectory temp;
	temp.MakeFile("keep.txt", "x");
	temp.MakeFile("drop.txt", "x");

	FileTree tree;
	tree.SetIgnoreQuery([](const std::filesystem::path& path, bool) { return path.filename() == "drop.txt"; });

	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	for (const FileNode& child : tree.GetRoot().Children)
	{
		if (child.Name == "drop.txt")
		{
			CHECK(child.Ignored);
		}
		else
		{
			CHECK_FALSE(child.Ignored);
		}
	}
}

TEST_CASE("The query is told whether the entry is a directory")
{
	TempDirectory temp;
	temp.MakeDir("folder");
	temp.MakeFile("file.txt", "x");

	std::vector<std::pair<std::string, bool>> seen;

	FileTree tree;
	tree.SetIgnoreQuery(
		[&seen](const std::filesystem::path& path, bool isDirectory) {
			seen.emplace_back(path.filename().string(), isDirectory);
			return false;
		});

	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	REQUIRE(seen.size() == 2);

	for (const auto& [name, isDirectory] : seen)
	{
		if (name == "folder")
		{
			CHECK(isDirectory);
		}
		else
		{
			CHECK_FALSE(isDirectory);
		}
	}
}

TEST_CASE("Refresh re-asks the query so a changed verdict takes effect")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	bool ignoreEverything = false;

	FileTree tree;
	tree.SetIgnoreQuery([&ignoreEverything](const std::filesystem::path&, bool) { return ignoreEverything; });

	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.GetRoot().Children.size() == 1);
	REQUIRE_FALSE(tree.GetRoot().Children[0].Ignored);

	// The rules file could have changed on disk between loads, so a carried-over node
	// must have its verdict refreshed rather than kept.
	ignoreEverything = true;
	tree.Refresh(tree.GetRoot());

	REQUIRE(tree.GetRoot().Children.size() == 1);
	CHECK(tree.GetRoot().Children[0].Ignored);
}

TEST_CASE("Refresh re-asks for nodes carried over from the previous listing")
{
	TempDirectory temp;
	temp.MakeFile("stays.txt", "x");
	temp.MakeDir("sub");

	int calls = 0;

	FileTree tree;
	tree.SetIgnoreQuery(
		[&calls](const std::filesystem::path&, bool) {
			++calls;
			return false;
		});

	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(calls == 2);

	calls = 0;
	tree.Refresh(tree.GetRoot());

	// Both entries survive the refresh, and both are asked about again.
	CHECK(calls == 2);
}

TEST_CASE("Rebase rewrites the root and every loaded descendant")
{
	TempDirectory temp;
	temp.MakeDir("sub");
	temp.MakeFile("sub/deep.txt", "x");
	temp.MakeFile("top.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	FileNode* sub = nullptr;

	for (FileNode& child : tree.GetRoot().Children)
	{
		if (child.Name == "sub")
		{
			sub = &child;
		}
	}

	REQUIRE(sub != nullptr);
	REQUIRE(tree.Expand(*sub).HasValue());

	const std::filesystem::path moved = temp.GetPath().parent_path() / "moved-elsewhere";
	tree.Rebase(moved);

	CHECK(tree.GetRoot().Path == moved);

	for (const FileNode& child : tree.GetRoot().Children)
	{
		CHECK(child.Path.parent_path() == moved);

		// The rewrite has to reach the whole subtree, not just the immediate children —
		// a half-rewritten tree would refresh some nodes and silently skip others.
		for (const FileNode& grandChild : child.Children)
		{
			CHECK(grandChild.Path.parent_path() == moved / child.Name);
		}
	}
}

TEST_CASE("Rebase leaves everything except the paths alone")
{
	TempDirectory temp;
	temp.MakeDir("sub");
	temp.MakeFile("top.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	FileNode* sub = nullptr;

	for (FileNode& child : tree.GetRoot().Children)
	{
		if (child.Name == "sub")
		{
			sub = &child;
		}
	}

	REQUIRE(sub != nullptr);
	REQUIRE(tree.Expand(*sub).HasValue());
	sub->Expanded = true;

	const std::size_t childCount = tree.GetRoot().Children.size();

	tree.Rebase(temp.GetPath().parent_path() / "moved-elsewhere");

	// The point of rebasing rather than reopening: the user's expansion state survives.
	REQUIRE(tree.GetRoot().Children.size() == childCount);

	for (const FileNode& child : tree.GetRoot().Children)
	{
		if (child.Name != "sub")
		{
			continue;
		}

		CHECK(child.Name == "sub");
		CHECK(child.IsDirectory);
		CHECK(child.Expanded);
		CHECK(child.Loaded);
	}
}

TEST_CASE("Rebase on a tree with no root does nothing")
{
	FileTree tree;

	tree.Rebase("D:/somewhere");

	CHECK_FALSE(tree.HasRoot());

	// Asserting on HasRoot alone would pass with the guard deleted, because nothing in
	// the rewrite touches that flag — it would just quietly write a path into a root
	// node that does not exist. The path is what proves the guard ran.
	CHECK(tree.GetRoot().Path.empty());
}

TEST_CASE("Replacing the query does not disturb already loaded nodes")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	FileTree tree;
	tree.SetIgnoreQuery([](const std::filesystem::path&, bool) { return true; });

	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.GetRoot().Children[0].Ignored);

	// The flag is cached, so swapping the query alone changes nothing until something
	// reloads. That is deliberate: the caller decides when to pay the cost.
	tree.SetIgnoreQuery([](const std::filesystem::path&, bool) { return false; });

	CHECK(tree.GetRoot().Children[0].Ignored);
}

TEST_CASE("Rebase renames the root node to match its new location")
{
	TempDirectory temp;
	const std::filesystem::path before = temp.MakeDir("before");

	FileTree tree;
	REQUIRE(tree.SetRoot(before).HasValue());
	REQUIRE(tree.GetRoot().Name == "before");

	tree.Rebase(temp.GetPath() / "after");

	// The root is the one node a rename can rename. Checking only the path would let the
	// tree go on displaying the old folder name beside a perfectly correct path — which
	// is what the panel actually shows the user.
	CHECK(tree.GetRoot().Name == "after");
	CHECK(tree.GetRoot().Path == temp.GetPath() / "after");
}

TEST_CASE("Rebase onto a drive root leaves the node with a usable name")
{
	TempDirectory temp;

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());

	tree.Rebase("D:/");

	// A drive root has no filename component, so the plain answer would be an empty
	// label rendering as a blank row.
	CHECK_FALSE(tree.GetRoot().Name.empty());
}

TEST_CASE("A root named with characters outside every ANSI code page is accepted")
{
	TempDirectory temp;
	const std::filesystem::path unicodeRoot = temp.GetPath() / L"中文-\U0001F600";
	std::filesystem::create_directory(unicodeRoot);
	std::filesystem::create_directory(unicodeRoot / L"中");

	FileTree tree;
	const auto result = tree.SetRoot(unicodeRoot);

	REQUIRE(result.HasValue());
	CHECK(tree.GetRoot().Name == Core::ToUtf8(unicodeRoot.filename()));

	CHECK_NOTHROW(static_cast<void>(tree.Expand(tree.GetRoot())));
	std::filesystem::remove_all(unicodeRoot / L"中");
}

TEST_CASE("Rejecting a file named with characters outside every ANSI code page does not throw")
{
	TempDirectory temp;
	const std::filesystem::path unicodeFile = temp.GetPath() / L"中文-\U0001F600.txt";
	std::ofstream{unicodeFile} << "x";

	FileTree tree;
	Core::Result<void> result = Core::Result<void>::Ok();

	CHECK_NOTHROW(result = tree.SetRoot(unicodeFile));
	CHECK_FALSE(result.HasValue());
}
