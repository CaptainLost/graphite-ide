#include <doctest/doctest.h>

#include "Core/Fs/FileTree.h"

#include "TempDirectory.h"

using core::FileNode;
using core::FileTree;

TEST_CASE("SetRoot creates a collapsed, unloaded root node")
{
    TempDirectory temp;
    temp.MakeFile("a.txt", "x");

    FileTree tree;
    const auto result = tree.SetRoot(temp.Path());

    REQUIRE(result.HasValue());
    CHECK(tree.HasRoot());
    CHECK(tree.Root().isDirectory);
    CHECK(tree.Root().path == temp.Path());
    CHECK_FALSE(tree.Root().expanded);
    CHECK_FALSE(tree.Root().loaded);
    CHECK(tree.Root().children.empty());
}

TEST_CASE("SetRoot rejects a path that is not a directory")
{
    TempDirectory temp;
    const auto file = temp.MakeFile("a.txt", "x");

    FileTree tree;
    const auto result = tree.SetRoot(file);

    REQUIRE_FALSE(result.HasValue());
    CHECK(result.GetError().code == core::ErrorCode::InvalidArgument);
    CHECK_FALSE(tree.HasRoot());
}

TEST_CASE("Expand loads children on first use and marks the node loaded")
{
    TempDirectory temp;
    temp.MakeFile("a.txt", "x");
    temp.MakeDir("sub");

    FileTree tree;
    REQUIRE(tree.SetRoot(temp.Path()).HasValue());

    const auto result = tree.Expand(tree.Root());

    REQUIRE(result.HasValue());
    CHECK(tree.Root().expanded);
    CHECK(tree.Root().loaded);
    CHECK(tree.Root().children.size() == 2);
}

TEST_CASE("Expand sorts directories before files, case-insensitively within each group")
{
    TempDirectory temp;
    temp.MakeFile("Zebra.txt", "x");
    temp.MakeFile("apple.txt", "x");
    temp.MakeDir("Yankee");
    temp.MakeDir("bravo");

    FileTree tree;
    REQUIRE(tree.SetRoot(temp.Path()).HasValue());
    REQUIRE(tree.Expand(tree.Root()).HasValue());

    const auto& children = tree.Root().children;
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
    REQUIRE(tree.SetRoot(temp.Path()).HasValue());
    REQUIRE(tree.Expand(tree.Root()).HasValue());

    // A file created after the first expansion must not appear until an explicit refresh.
    temp.MakeFile("b.txt", "x");
    tree.Collapse(tree.Root());
    REQUIRE(tree.Expand(tree.Root()).HasValue());

    CHECK(tree.Root().children.size() == 1);
}

TEST_CASE("Collapse clears the expanded flag but keeps loaded children")
{
    TempDirectory temp;
    temp.MakeFile("a.txt", "x");

    FileTree tree;
    REQUIRE(tree.SetRoot(temp.Path()).HasValue());
    REQUIRE(tree.Expand(tree.Root()).HasValue());

    tree.Collapse(tree.Root());

    CHECK_FALSE(tree.Root().expanded);
    CHECK(tree.Root().loaded);
    CHECK(tree.Root().children.size() == 1);
}

TEST_CASE("Expanding an unreadable directory marks it inaccessible instead of failing")
{
    TempDirectory temp;
    const auto missing = temp.Path() / "vanished";

    FileTree tree;
    REQUIRE(tree.SetRoot(temp.Path()).HasValue());
    REQUIRE(tree.Expand(tree.Root()).HasValue());

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
    REQUIRE(tree.SetRoot(temp.Path()).HasValue());
    REQUIRE(tree.Expand(tree.Root()).HasValue());
    REQUIRE(tree.Root().children.size() == 1);

    temp.MakeFile("b.txt", "x");
    tree.Refresh(tree.Root());

    CHECK(tree.Root().children.size() == 2);
}

TEST_CASE("Refresh drops nodes that disappeared from disk")
{
    TempDirectory temp;
    const auto doomed = temp.MakeFile("gone.txt", "x");
    temp.MakeFile("stays.txt", "x");

    FileTree tree;
    REQUIRE(tree.SetRoot(temp.Path()).HasValue());
    REQUIRE(tree.Expand(tree.Root()).HasValue());

    std::error_code code;
    std::filesystem::remove(doomed, code);
    tree.Refresh(tree.Root());

    REQUIRE(tree.Root().children.size() == 1);
    CHECK(tree.Root().children[0].name == "stays.txt");
}

TEST_CASE("Refresh keeps an expanded subdirectory expanded and loaded")
{
    TempDirectory temp;
    temp.MakeFile("sub/inner.txt", "x");

    FileTree tree;
    REQUIRE(tree.SetRoot(temp.Path()).HasValue());
    REQUIRE(tree.Expand(tree.Root()).HasValue());
    REQUIRE(tree.Root().children.size() == 1);
    REQUIRE(tree.Expand(tree.Root().children[0]).HasValue());
    REQUIRE(tree.Root().children[0].children.size() == 1);

    temp.MakeFile("second.txt", "x");
    tree.Refresh(tree.Root());

    REQUIRE(tree.Root().children.size() == 2);
    const FileNode& sub = tree.Root().children[0];
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
    REQUIRE(tree.SetRoot(temp.Path()).HasValue());
    REQUIRE(tree.Expand(tree.Root()).HasValue());
    REQUIRE(tree.Expand(tree.Root().children[0]).HasValue());

    temp.MakeFile("sub/two.txt", "x");
    tree.Refresh(tree.Root());

    CHECK(tree.Root().children[0].children.size() == 2);
}

TEST_CASE("Refresh leaves a collapsed, never-opened subdirectory unloaded")
{
    TempDirectory temp;
    temp.MakeFile("sub/inner.txt", "x");

    FileTree tree;
    REQUIRE(tree.SetRoot(temp.Path()).HasValue());
    REQUIRE(tree.Expand(tree.Root()).HasValue());
    tree.Refresh(tree.Root());

    const FileNode& sub = tree.Root().children[0];
    CHECK_FALSE(sub.expanded);
    CHECK_FALSE(sub.loaded);
    CHECK(sub.children.empty());
}

TEST_CASE("Refresh on a node that was never loaded does nothing")
{
    TempDirectory temp;
    temp.MakeFile("a.txt", "x");

    FileTree tree;
    REQUIRE(tree.SetRoot(temp.Path()).HasValue());

    tree.Refresh(tree.Root());

    CHECK_FALSE(tree.Root().loaded);
    CHECK(tree.Root().children.empty());
}
