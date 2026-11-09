#include <doctest/doctest.h>

#include "Git/Worktrees/WorktreeOfFile.h"

#include "TempDirectory.h"

#include <optional>
#include <string>
#include <vector>

namespace {
	Core::WorktreeEntry EntryAt(const char* path)
	{
		Core::WorktreeEntry entry;
		entry.Path = path;
		return entry;
	}

	const std::filesystem::path kOpenFolder = "D:/r";
}

TEST_CASE("A file in a worktree nested inside the open folder belongs to that worktree")
{
	const std::vector<Core::WorktreeEntry> entries{EntryAt("D:/r"), EntryAt("D:/r/.claude/worktrees/a")};

	const std::optional<std::string> folder =
		Graphite::WorktreeFolderOf("D:/r/.claude/worktrees/a/src/x.cpp", entries, kOpenFolder);

	REQUIRE(folder.has_value());
	CHECK(*folder == "a");
}

TEST_CASE("A file only the open folder holds belongs to no other worktree")
{
	const std::vector<Core::WorktreeEntry> entries{EntryAt("D:/r"), EntryAt("D:/r/.claude/worktrees/a")};

	CHECK_FALSE(Graphite::WorktreeFolderOf("D:/r/src/x.cpp", entries, kOpenFolder).has_value());
	CHECK_FALSE(Graphite::WorktreeFolderOf("D:/elsewhere/x.cpp", entries, kOpenFolder).has_value());
}

TEST_CASE("Of nested worktrees the deepest one holding the file wins")
{
	const std::vector<Core::WorktreeEntry> entries{EntryAt("D:/r"), EntryAt("D:/r/w/a"), EntryAt("D:/r/w/a/inner/b")};

	const std::optional<std::string> folder = Graphite::WorktreeFolderOf("D:/r/w/a/inner/b/x.cpp", entries, kOpenFolder);

	REQUIRE(folder.has_value());
	CHECK(*folder == "b");
}

TEST_CASE("A worktree is found whatever the case and separators of the file's spelling")
{
	const std::vector<Core::WorktreeEntry> entries{EntryAt("D:/r"), EntryAt("D:/r/w/a")};

	const std::optional<std::string> folder = Graphite::WorktreeFolderOf("d:\\R\\W\\A\\x.cpp", entries, kOpenFolder);

	REQUIRE(folder.has_value());
	CHECK(*folder == "a");
}

TEST_CASE("A sibling folder sharing a worktree's name prefix is not in it")
{
	const std::vector<Core::WorktreeEntry> entries{EntryAt("D:/r"), EntryAt("D:/r/w/a")};

	CHECK_FALSE(Graphite::WorktreeFolderOf("D:/r/w/ab/x.cpp", entries, kOpenFolder).has_value());
}

TEST_CASE("A directory with a .git file or folder between a file and the working directory is a nested checkout")
{
	TempDirectory folder;
	folder.MakeFile("plain/sub/x.cpp", "x");
	folder.MakeFile("linked/.git", "gitdir: elsewhere");
	folder.MakeFile("linked/src/x.cpp", "x");
	folder.MakeFile("cloned/.git/HEAD", "ref");
	folder.MakeFile("cloned/y.cpp", "y");

	CHECK_FALSE(Graphite::IsInsideNestedCheckout(folder.GetPath(), "plain/sub/x.cpp"));
	CHECK_FALSE(Graphite::IsInsideNestedCheckout(folder.GetPath(), "top.cpp"));
	CHECK(Graphite::IsInsideNestedCheckout(folder.GetPath(), "linked/src/x.cpp"));
	CHECK(Graphite::IsInsideNestedCheckout(folder.GetPath(), "linked/.git"));
	CHECK(Graphite::IsInsideNestedCheckout(folder.GetPath(), "cloned/y.cpp"));
}

TEST_CASE("The working directory's own .git entry does not make its files nested")
{
	TempDirectory folder;
	folder.MakeFile(".git/HEAD", "ref");
	folder.MakeFile("src/x.cpp", "x");

	CHECK_FALSE(Graphite::IsInsideNestedCheckout(folder.GetPath(), "src/x.cpp"));
}
