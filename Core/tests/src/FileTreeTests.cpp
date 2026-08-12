#include <doctest/doctest.h>

#include "Core/Fs/FileTree.h"

#include "TempDirectory.h"

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
	CHECK(tree.GetRoot().isDirectory);
	CHECK(tree.GetRoot().path == temp.GetPath());
	CHECK_FALSE(tree.GetRoot().expanded);
	CHECK_FALSE(tree.GetRoot().loaded);
	CHECK(tree.GetRoot().children.empty());
}

TEST_CASE("SetRoot rejects a path that is not a directory")
{
	TempDirectory temp;
	const auto file = temp.MakeFile("a.txt", "x");

	FileTree tree;
	const auto result = tree.SetRoot(file);

	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().code == Core::ErrorCode::InvalidArgument);
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
	CHECK(tree.GetRoot().expanded);
	CHECK(tree.GetRoot().loaded);
	CHECK(tree.GetRoot().children.size() == 2);
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

	const auto& children = tree.GetRoot().children;
	REQUIRE(children.size() == 4);
	CHECK(children[0].name == "bravo");
	CHECK(children[1].name == "Yankee");
	CHECK(children[2].name == "apple.txt");
	CHECK(children[3].name == "Zebra.txt");
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

	CHECK(tree.GetRoot().children.size() == 1);
}

TEST_CASE("Collapse clears the expanded flag but keeps loaded children")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	tree.Collapse(tree.GetRoot());

	CHECK_FALSE(tree.GetRoot().expanded);
	CHECK(tree.GetRoot().loaded);
	CHECK(tree.GetRoot().children.size() == 1);
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
	ghost.path = missing;
	ghost.name = "vanished";
	ghost.isDirectory = true;

	const auto result = tree.Expand(ghost);

	REQUIRE(result.HasValue());
	CHECK_FALSE(ghost.accessible);
	CHECK(ghost.loaded);
	CHECK(ghost.children.empty());
}

TEST_CASE("Refresh picks up a file created after the first expansion")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.GetRoot().children.size() == 1);

	temp.MakeFile("b.txt", "x");
	tree.Refresh(tree.GetRoot());

	CHECK(tree.GetRoot().children.size() == 2);
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

	REQUIRE(tree.GetRoot().children.size() == 1);
	CHECK(tree.GetRoot().children[0].name == "stays.txt");
}

TEST_CASE("Refresh keeps an expanded subdirectory expanded and loaded")
{
	TempDirectory temp;
	temp.MakeFile("sub/inner.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.GetRoot().children.size() == 1);
	REQUIRE(tree.Expand(tree.GetRoot().children[0]).HasValue());
	REQUIRE(tree.GetRoot().children[0].children.size() == 1);

	temp.MakeFile("second.txt", "x");
	tree.Refresh(tree.GetRoot());

	REQUIRE(tree.GetRoot().children.size() == 2);
	const FileNode& sub = tree.GetRoot().children[0];
	CHECK(sub.name == "sub");
	CHECK(sub.expanded);
	CHECK(sub.loaded);
	REQUIRE(sub.children.size() == 1);
	CHECK(sub.children[0].name == "inner.txt");
}

TEST_CASE("Refresh re-reads an expanded subdirectory so its own changes appear")
{
	TempDirectory temp;
	temp.MakeFile("sub/one.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot().children[0]).HasValue());

	temp.MakeFile("sub/two.txt", "x");
	tree.Refresh(tree.GetRoot());

	CHECK(tree.GetRoot().children[0].children.size() == 2);
}

TEST_CASE("Refresh leaves a collapsed, never-opened subdirectory unloaded")
{
	TempDirectory temp;
	temp.MakeFile("sub/inner.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	tree.Refresh(tree.GetRoot());

	const FileNode& sub = tree.GetRoot().children[0];
	CHECK_FALSE(sub.expanded);
	CHECK_FALSE(sub.loaded);
	CHECK(sub.children.empty());
}

TEST_CASE("Refresh on a node that was never loaded does nothing")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());

	tree.Refresh(tree.GetRoot());

	CHECK_FALSE(tree.GetRoot().loaded);
	CHECK(tree.GetRoot().children.empty());
}

TEST_CASE("Without a query nothing is ignored")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	REQUIRE(tree.GetRoot().children.size() == 1);
	CHECK_FALSE(tree.GetRoot().children[0].ignored);
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

	for (const FileNode& child : tree.GetRoot().children)
	{
		if (child.name == "drop.txt")
			CHECK(child.ignored);
		else
			CHECK_FALSE(child.ignored);
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
			CHECK(isDirectory);
		else
			CHECK_FALSE(isDirectory);
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
	REQUIRE(tree.GetRoot().children.size() == 1);
	REQUIRE_FALSE(tree.GetRoot().children[0].ignored);

	// The rules file could have changed on disk between loads, so a carried-over node
	// must have its verdict refreshed rather than kept.
	ignoreEverything = true;
	tree.Refresh(tree.GetRoot());

	REQUIRE(tree.GetRoot().children.size() == 1);
	CHECK(tree.GetRoot().children[0].ignored);
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

	for (FileNode& child : tree.GetRoot().children)
	{
		if (child.name == "sub")
			sub = &child;
	}

	REQUIRE(sub != nullptr);
	REQUIRE(tree.Expand(*sub).HasValue());

	const std::filesystem::path moved = temp.GetPath().parent_path() / "moved-elsewhere";
	tree.Rebase(moved);

	CHECK(tree.GetRoot().path == moved);

	for (const FileNode& child : tree.GetRoot().children)
	{
		CHECK(child.path.parent_path() == moved);

		// The rewrite has to reach the whole subtree, not just the immediate children —
		// a half-rewritten tree would refresh some nodes and silently skip others.
		for (const FileNode& grandChild : child.children)
			CHECK(grandChild.path.parent_path() == moved / child.name);
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

	for (FileNode& child : tree.GetRoot().children)
	{
		if (child.name == "sub")
			sub = &child;
	}

	REQUIRE(sub != nullptr);
	REQUIRE(tree.Expand(*sub).HasValue());
	sub->expanded = true;

	const std::size_t childCount = tree.GetRoot().children.size();

	tree.Rebase(temp.GetPath().parent_path() / "moved-elsewhere");

	// The point of rebasing rather than reopening: the user's expansion state survives.
	REQUIRE(tree.GetRoot().children.size() == childCount);

	for (const FileNode& child : tree.GetRoot().children)
	{
		if (child.name != "sub")
			continue;

		CHECK(child.name == "sub");
		CHECK(child.isDirectory);
		CHECK(child.expanded);
		CHECK(child.loaded);
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
	CHECK(tree.GetRoot().path.empty());
}

TEST_CASE("Replacing the query does not disturb already loaded nodes")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "x");

	FileTree tree;
	tree.SetIgnoreQuery([](const std::filesystem::path&, bool) { return true; });

	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.GetRoot().children[0].ignored);

	// The flag is cached, so swapping the query alone changes nothing until something
	// reloads. That is deliberate: the caller decides when to pay the cost.
	tree.SetIgnoreQuery([](const std::filesystem::path&, bool) { return false; });

	CHECK(tree.GetRoot().children[0].ignored);
}

TEST_CASE("Rebase renames the root node to match its new location")
{
	TempDirectory temp;
	const std::filesystem::path before = temp.MakeDir("before");

	FileTree tree;
	REQUIRE(tree.SetRoot(before).HasValue());
	REQUIRE(tree.GetRoot().name == "before");

	tree.Rebase(temp.GetPath() / "after");

	// The root is the one node a rename can rename. Checking only the path would let the
	// tree go on displaying the old folder name beside a perfectly correct path — which
	// is what the panel actually shows the user.
	CHECK(tree.GetRoot().name == "after");
	CHECK(tree.GetRoot().path == temp.GetPath() / "after");
}

TEST_CASE("Rebase onto a drive root leaves the node with a usable name")
{
	TempDirectory temp;

	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()).HasValue());

	tree.Rebase("D:/");

	// A drive root has no filename component, so the plain answer would be an empty
	// label rendering as a blank row.
	CHECK_FALSE(tree.GetRoot().name.empty());
}
