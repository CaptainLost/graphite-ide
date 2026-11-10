#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/FileTree.h"

#include "TempDirectory.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Core::FileNode;
using Core::FileTree;

namespace {

	struct QueriedEntry
	{
		std::string FileName;
		bool IsDirectory = false;
	};

	FileNode* FindChildNamed(FileNode& parent, const std::string& childName)
	{
		for (FileNode& child : parent.Children)
		{
			if (child.Name == childName)
			{
				return &child;
			}
		}

		return nullptr;
	}
}

TEST_CASE("SetRoot creates a collapsed, unloaded root node")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("a.txt", "x");

	FileTree tree;
	const Core::Result<void> result = tree.SetRoot(temporaryDirectory.GetPath());

	REQUIRE(result.HasValue());
	CHECK(tree.HasRoot());
	CHECK(tree.GetRoot().IsDirectory);
	CHECK(tree.GetRoot().Path == temporaryDirectory.GetPath());
	CHECK_FALSE(tree.GetRoot().Expanded);
	CHECK_FALSE(tree.GetRoot().ChildrenLoaded);
	CHECK(tree.GetRoot().Children.empty());
}

TEST_CASE("SetRoot rejects a path that is not a directory")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path file = temporaryDirectory.MakeFile("a.txt", "x");

	FileTree tree;
	const Core::Result<void> result = tree.SetRoot(file);

	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK_FALSE(tree.HasRoot());
}

TEST_CASE("Expand loads children on first use and marks the node loaded")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("a.txt", "x");
	temporaryDirectory.MakeDirectory("sub");

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());

	const Core::Result<void> result = tree.Expand(tree.GetRoot());

	REQUIRE(result.HasValue());
	CHECK(tree.GetRoot().Expanded);
	CHECK(tree.GetRoot().ChildrenLoaded);
	CHECK(tree.GetRoot().Children.size() == 2);
}

TEST_CASE("Expand sorts directories before files, case-insensitively within each group")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("Zebra.txt", "x");
	temporaryDirectory.MakeFile("apple.txt", "x");
	temporaryDirectory.MakeDirectory("Yankee");
	temporaryDirectory.MakeDirectory("bravo");

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	const std::vector<FileNode>& children = tree.GetRoot().Children;
	REQUIRE(children.size() == 4);
	CHECK(children[0].Name == "bravo");
	CHECK(children[1].Name == "Yankee");
	CHECK(children[2].Name == "apple.txt");
	CHECK(children[3].Name == "Zebra.txt");
}

TEST_CASE("Expand on an already loaded node does not re-read the disk so a file created since stays hidden until an explicit refresh")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	temporaryDirectory.MakeFile("b.txt", "x");
	tree.Collapse(tree.GetRoot());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	CHECK(tree.GetRoot().Children.size() == 1);
}

TEST_CASE("Collapse clears the expanded flag but keeps loaded children")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	tree.Collapse(tree.GetRoot());

	CHECK_FALSE(tree.GetRoot().Expanded);
	CHECK(tree.GetRoot().ChildrenLoaded);
	CHECK(tree.GetRoot().Children.size() == 1);
}

TEST_CASE("Expanding an unreadable directory marks it not readable instead of failing so one bad directory does not break browsing the rest")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path vanishedDirectory = temporaryDirectory.GetPath() / "vanished";

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	FileNode nodeOfVanishedDirectory{
		.Path = vanishedDirectory,
		.Name = "vanished",
		.IsDirectory = true,
	};

	const Core::Result<void> result = tree.Expand(nodeOfVanishedDirectory);

	REQUIRE(result.HasValue());
	CHECK_FALSE(nodeOfVanishedDirectory.Readable);
	CHECK(nodeOfVanishedDirectory.ChildrenLoaded);
	CHECK(nodeOfVanishedDirectory.Children.empty());
}

TEST_CASE("Refresh picks up a file created after the first expansion")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.GetRoot().Children.size() == 1);

	temporaryDirectory.MakeFile("b.txt", "x");
	tree.Refresh(tree.GetRoot());

	CHECK(tree.GetRoot().Children.size() == 2);
}

TEST_CASE("Refresh drops nodes that disappeared from disk")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path doomed = temporaryDirectory.MakeFile("gone.txt", "x");
	temporaryDirectory.MakeFile("stays.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	std::error_code ignoredRemoveError;
	std::filesystem::remove(doomed, ignoredRemoveError);
	tree.Refresh(tree.GetRoot());

	REQUIRE(tree.GetRoot().Children.size() == 1);
	CHECK(tree.GetRoot().Children[0].Name == "stays.txt");
}

TEST_CASE("Refresh keeps an expanded subdirectory expanded and loaded")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("sub/inner.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.GetRoot().Children.size() == 1);
	REQUIRE(tree.Expand(tree.GetRoot().Children[0]).HasValue());
	REQUIRE(tree.GetRoot().Children[0].Children.size() == 1);

	temporaryDirectory.MakeFile("second.txt", "x");
	tree.Refresh(tree.GetRoot());

	REQUIRE(tree.GetRoot().Children.size() == 2);
	const FileNode& subdirectory = tree.GetRoot().Children[0];
	CHECK(subdirectory.Name == "sub");
	CHECK(subdirectory.Expanded);
	CHECK(subdirectory.ChildrenLoaded);
	REQUIRE(subdirectory.Children.size() == 1);
	CHECK(subdirectory.Children[0].Name == "inner.txt");
}

TEST_CASE("Refresh re-reads an expanded subdirectory so its own changes appear")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("sub/one.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot().Children[0]).HasValue());

	temporaryDirectory.MakeFile("sub/two.txt", "x");
	tree.Refresh(tree.GetRoot());

	CHECK(tree.GetRoot().Children[0].Children.size() == 2);
}

TEST_CASE("Refresh leaves a collapsed, never-opened subdirectory unloaded")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("sub/inner.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	tree.Refresh(tree.GetRoot());

	const FileNode& subdirectory = tree.GetRoot().Children[0];
	CHECK_FALSE(subdirectory.Expanded);
	CHECK_FALSE(subdirectory.ChildrenLoaded);
	CHECK(subdirectory.Children.empty());
}

TEST_CASE("Refresh on a node that was never loaded does nothing")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());

	tree.Refresh(tree.GetRoot());

	CHECK_FALSE(tree.GetRoot().ChildrenLoaded);
	CHECK(tree.GetRoot().Children.empty());
}

TEST_CASE("Without a query nothing is ignored because a directory outside any repository has no ignore rules")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("a.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	REQUIRE(tree.GetRoot().Children.size() == 1);
	CHECK_FALSE(tree.GetRoot().Children[0].Ignored);
}

TEST_CASE("Expand marks the children the query rejects")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("keep.txt", "x");
	temporaryDirectory.MakeFile("drop.txt", "x");

	FileTree tree;
	tree.SetIgnoreQuery([](const std::filesystem::path& path, bool) { return path.filename() == "drop.txt"; });

	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
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
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeDirectory("folder");
	temporaryDirectory.MakeFile("file.txt", "x");

	std::vector<QueriedEntry> queriedEntries;

	FileTree tree;
	tree.SetIgnoreQuery(
		[&queriedEntries](const std::filesystem::path& path, bool isDirectory) {
			queriedEntries.push_back(QueriedEntry{.FileName = path.filename().string(), .IsDirectory = isDirectory});
			return false;
		});

	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	REQUIRE(queriedEntries.size() == 2);

	for (const QueriedEntry& queried : queriedEntries)
	{
		if (queried.FileName == "folder")
		{
			CHECK(queried.IsDirectory);
		}
		else
		{
			CHECK_FALSE(queried.IsDirectory);
		}
	}
}

TEST_CASE("A refresh told to ask again re-asks the query for a carried-over node so a rules file changed on disk since the last load takes effect")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("a.txt", "x");

	bool ignoreEverything = false;

	FileTree tree;
	tree.SetIgnoreQuery([&ignoreEverything](const std::filesystem::path&, bool) { return ignoreEverything; });

	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.GetRoot().Children.size() == 1);
	REQUIRE_FALSE(tree.GetRoot().Children[0].Ignored);

	ignoreEverything = true;
	tree.Refresh(tree.GetRoot(), Core::IgnoreAnswers::AskAgain);

	REQUIRE(tree.GetRoot().Children.size() == 1);
	CHECK(tree.GetRoot().Children[0].Ignored);
}

TEST_CASE("A refresh that keeps the answers leaves a carried-over node's answer as it was even when the query would now answer otherwise")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("a.txt", "x");

	bool ignoreEverything = false;

	FileTree tree;
	tree.SetIgnoreQuery([&ignoreEverything](const std::filesystem::path&, bool) { return ignoreEverything; });

	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	ignoreEverything = true;
	tree.Refresh(tree.GetRoot(), Core::IgnoreAnswers::Keep);

	REQUIRE(tree.GetRoot().Children.size() == 1);
	CHECK_FALSE(tree.GetRoot().Children[0].Ignored);
}

TEST_CASE("A refresh that keeps the answers asks only about the entries it did not know, in every loaded folder")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("known.txt", "x");
	temporaryDirectory.MakeFile("sub/known-inside.txt", "x");

	std::vector<std::string> askedAbout;

	FileTree tree;
	tree.SetIgnoreQuery([&askedAbout](const std::filesystem::path& path, bool) {
		askedAbout.push_back(Core::ToUtf8(path.filename()));
		return false;
	});

	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	FileNode* subdirectory = FindChildNamed(tree.GetRoot(), "sub");
	REQUIRE(subdirectory != nullptr);
	REQUIRE(tree.Expand(*subdirectory).HasValue());

	temporaryDirectory.MakeFile("new.txt", "x");
	temporaryDirectory.MakeFile("sub/new-inside.txt", "x");
	askedAbout.clear();

	tree.Refresh(tree.GetRoot());

	std::sort(askedAbout.begin(), askedAbout.end());
	CHECK(askedAbout == std::vector<std::string>{"new-inside.txt", "new.txt"});
}

TEST_CASE("A refresh told to ask again re-asks the query about every node carried over from the previous listing")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("stays.txt", "x");
	temporaryDirectory.MakeDirectory("sub");

	const int entriesOnDisk = 2;
	int queryCalls = 0;

	FileTree tree;
	tree.SetIgnoreQuery(
		[&queryCalls](const std::filesystem::path&, bool) {
			++queryCalls;
			return false;
		});

	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(queryCalls == entriesOnDisk);

	queryCalls = 0;
	tree.Refresh(tree.GetRoot(), Core::IgnoreAnswers::AskAgain);

	CHECK(queryCalls == entriesOnDisk);
}

TEST_CASE("A refresh told to ask again works out an ignored folder's entries again from the folder's new answer")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("build/output.o", "x");

	std::string ignoredName = "build";

	FileTree tree;
	tree.SetIgnoreQuery([&ignoredName](const std::filesystem::path& path, bool) {
		return Core::ToUtf8(path.filename()) == ignoredName;
	});

	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	FileNode* build = FindChildNamed(tree.GetRoot(), "build");
	REQUIRE(build != nullptr);
	REQUIRE(tree.Expand(*build).HasValue());
	REQUIRE(build->Children.size() == 1);
	REQUIRE(build->Children[0].Ignored);

	ignoredName = "nothing";
	tree.Refresh(tree.GetRoot(), Core::IgnoreAnswers::AskAgain);

	build = FindChildNamed(tree.GetRoot(), "build");
	REQUIRE(build != nullptr);
	CHECK_FALSE(build->Ignored);
	REQUIRE(build->Children.size() == 1);
	CHECK_FALSE(build->Children[0].Ignored);
}

TEST_CASE("A refresh of a folder of many entries keeps each carried-over node with its own loaded children")
{
	TempDirectory temporaryDirectory;
	constexpr int kFolderCount = 300;

	for (int folderNumber = 0; folderNumber < kFolderCount; ++folderNumber)
	{
		temporaryDirectory.MakeFile("folder-" + std::to_string(folderNumber) + "/inside-" + std::to_string(folderNumber) + ".txt", "x");
	}

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	for (FileNode& folder : tree.GetRoot().Children)
	{
		REQUIRE(tree.Expand(folder).HasValue());
	}

	tree.Refresh(tree.GetRoot());

	REQUIRE(tree.GetRoot().Children.size() == kFolderCount);

	for (const FileNode& folder : tree.GetRoot().Children)
	{
		CAPTURE(folder.Name);
		CHECK(folder.ChildrenLoaded);
		REQUIRE(folder.Children.size() == 1);
		CHECK(folder.Children[0].Path.parent_path() == folder.Path);
	}
}

namespace {
	struct CountedIgnoreQuery
	{
		std::vector<std::string> AskedAbout;
		std::string IgnoredName;

		Core::IgnoreQuery Query()
		{
			return [this](const std::filesystem::path& path, bool) {
				const std::string fileName = Core::ToUtf8(path.filename());
				AskedAbout.push_back(fileName);
				return fileName == IgnoredName;
			};
		}

		bool WasAskedAbout(const std::string& fileName) const
		{
			for (const std::string& asked : AskedAbout)
			{
				if (asked == fileName)
				{
					return true;
				}
			}

			return false;
		}
	};
}

TEST_CASE("Expanding an ignored folder marks its entries ignored without asking about any of them")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("build/output.o", "x");
	temporaryDirectory.MakeFile("build/nested/deeper.o", "x");
	temporaryDirectory.MakeFile("source.cpp", "x");

	CountedIgnoreQuery counted{.IgnoredName = "build"};

	FileTree tree;
	tree.SetIgnoreQuery(counted.Query());

	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	FileNode* build = FindChildNamed(tree.GetRoot(), "build");
	REQUIRE(build != nullptr);
	REQUIRE(build->Ignored);

	counted.AskedAbout.clear();
	REQUIRE(tree.Expand(*build).HasValue());

	FileNode* nested = FindChildNamed(*build, "nested");
	REQUIRE(nested != nullptr);
	REQUIRE(tree.Expand(*nested).HasValue());

	CHECK(counted.AskedAbout.empty());

	const FileNode* output = FindChildNamed(*build, "output.o");
	REQUIRE(output != nullptr);
	CHECK(output->Ignored);
	CHECK(nested->Ignored);
	REQUIRE(nested->Children.size() == 1);
	CHECK(nested->Children[0].Ignored);

	const FileNode* source = FindChildNamed(tree.GetRoot(), "source.cpp");
	REQUIRE(source != nullptr);
	CHECK_FALSE(source->Ignored);
}

TEST_CASE("A new entry in an ignored folder is marked ignored by a refresh without asking about it")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("build/output.o", "x");

	CountedIgnoreQuery counted{.IgnoredName = "build"};

	FileTree tree;
	tree.SetIgnoreQuery(counted.Query());

	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	FileNode* build = FindChildNamed(tree.GetRoot(), "build");
	REQUIRE(build != nullptr);
	REQUIRE(tree.Expand(*build).HasValue());

	temporaryDirectory.MakeFile("build/fresh.o", "x");
	counted.AskedAbout.clear();

	tree.Refresh(*build);

	CHECK_FALSE(counted.WasAskedAbout("fresh.o"));
	const FileNode* fresh = FindChildNamed(*build, "fresh.o");
	REQUIRE(fresh != nullptr);
	CHECK(fresh->Ignored);
}

TEST_CASE("Rebase rewrites the root and every loaded descendant and not just the immediate children so a later refresh reaches the whole subtree")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeDirectory("sub");
	temporaryDirectory.MakeFile("sub/deep.txt", "x");
	temporaryDirectory.MakeFile("top.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	FileNode* subdirectory = FindChildNamed(tree.GetRoot(), "sub");

	REQUIRE(subdirectory != nullptr);
	REQUIRE(tree.Expand(*subdirectory).HasValue());

	const std::filesystem::path movedRoot = temporaryDirectory.GetPath().parent_path() / "moved-elsewhere";
	tree.Rebase(movedRoot);

	CHECK(tree.GetRoot().Path == movedRoot);

	for (const FileNode& child : tree.GetRoot().Children)
	{
		CHECK(child.Path.parent_path() == movedRoot);

		for (const FileNode& grandChild : child.Children)
		{
			CHECK(grandChild.Path.parent_path() == movedRoot / child.Name);
		}
	}
}

TEST_CASE("Rebase leaves everything except the paths and the root's own name alone so the user's expansion state survives where reopening would lose it")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeDirectory("sub");
	temporaryDirectory.MakeFile("top.txt", "x");

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());

	FileNode* subdirectory = FindChildNamed(tree.GetRoot(), "sub");

	REQUIRE(subdirectory != nullptr);
	REQUIRE(tree.Expand(*subdirectory).HasValue());

	const std::size_t childCount = tree.GetRoot().Children.size();

	const std::filesystem::path movedRoot = temporaryDirectory.GetPath().parent_path() / "moved-elsewhere";
	tree.Rebase(movedRoot);

	REQUIRE(tree.GetRoot().Children.size() == childCount);
	const FileNode* subdirectoryAfterRebase = FindChildNamed(tree.GetRoot(), "sub");

	REQUIRE(subdirectoryAfterRebase != nullptr);
	CHECK(subdirectoryAfterRebase->IsDirectory);
	CHECK(subdirectoryAfterRebase->Expanded);
	CHECK(subdirectoryAfterRebase->ChildrenLoaded);
}

TEST_CASE("Rebase on a tree with no root does nothing and the root path staying empty is what proves it since the rewrite never touches HasRoot")
{
	FileTree tree;

	tree.Rebase("D:/somewhere");

	CHECK_FALSE(tree.HasRoot());
	CHECK(tree.GetRoot().Path.empty());
}

TEST_CASE("Replacing the query does not disturb already loaded nodes whose verdict stays cached until the caller chooses to reload")
{
	TempDirectory temporaryDirectory;
	temporaryDirectory.MakeFile("a.txt", "x");

	FileTree tree;
	tree.SetIgnoreQuery([](const std::filesystem::path&, bool) { return true; });

	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());
	REQUIRE(tree.Expand(tree.GetRoot()).HasValue());
	REQUIRE(tree.GetRoot().Children[0].Ignored);

	tree.SetIgnoreQuery([](const std::filesystem::path&, bool) { return false; });

	CHECK(tree.GetRoot().Children[0].Ignored);
}

TEST_CASE("Rebase renames the root node to match its new location since the name is what the panel shows beside the path")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path before = temporaryDirectory.MakeDirectory("before");

	FileTree tree;
	REQUIRE(tree.SetRoot(before).HasValue());
	REQUIRE(tree.GetRoot().Name == "before");

	tree.Rebase(temporaryDirectory.GetPath() / "after");

	CHECK(tree.GetRoot().Name == "after");
	CHECK(tree.GetRoot().Path == temporaryDirectory.GetPath() / "after");
}

TEST_CASE("Rebase onto a drive root which has no filename component still leaves the node a non-empty name rather than a blank row")
{
	TempDirectory temporaryDirectory;

	FileTree tree;
	REQUIRE(tree.SetRoot(temporaryDirectory.GetPath()).HasValue());

	tree.Rebase("D:/");

	CHECK_FALSE(tree.GetRoot().Name.empty());
}

TEST_CASE("A root named with characters outside every ANSI code page is accepted")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path unicodeRoot = temporaryDirectory.GetPath() / L"中文-\U0001F600";
	std::filesystem::create_directory(unicodeRoot);
	std::filesystem::create_directory(unicodeRoot / L"中");

	FileTree tree;
	const Core::Result<void> result = tree.SetRoot(unicodeRoot);

	REQUIRE(result.HasValue());
	CHECK(tree.GetRoot().Name == Core::ToUtf8(unicodeRoot.filename()));

	CHECK_NOTHROW(static_cast<void>(tree.Expand(tree.GetRoot())));
	std::filesystem::remove_all(unicodeRoot / L"中");
}

TEST_CASE("Rejecting a file named with characters outside every ANSI code page does not throw")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path unicodeFile = temporaryDirectory.GetPath() / L"中文-\U0001F600.txt";
	std::ofstream{unicodeFile} << "x";

	FileTree tree;
	Core::Result<void> result = Core::Result<void>::Ok();

	CHECK_NOTHROW(result = tree.SetRoot(unicodeFile));
	CHECK_FALSE(result.HasValue());
}

TEST_CASE("A folder's entries are offered to the folder query before any of them is asked about")
{
	TempDirectory temp;
	std::filesystem::create_directories(temp.GetPath() / "skipped");
	std::ofstream(temp.GetPath() / "a.txt") << "x";
	std::ofstream(temp.GetPath() / "skipped" / "inner.txt") << "x";
	FileTree tree;
	REQUIRE(tree.SetRoot(temp.GetPath()));
	std::vector<std::string> offered;
	std::vector<std::string> asked;
	tree.SetIgnoreFolderQuery([&](const std::filesystem::path& folder, const std::vector<Core::DirectoryEntry>& entries) {
		offered.push_back(folder.filename().generic_string() + ":" + std::to_string(entries.size())); // not a path conversion
	});
	tree.SetIgnoreQuery([&](const std::filesystem::path& path, bool) {
		asked.push_back(path.filename().generic_string()); // not a path conversion
		CHECK_FALSE(offered.empty());
		return path.filename() == "skipped";
	});

	REQUIRE(tree.Expand(tree.GetRoot()));
	CHECK(offered.size() == 1);
	CHECK(offered.back().ends_with(":2"));

	SUBCASE("a refresh with nothing new asks nothing")
	{
		tree.Refresh(tree.GetRoot(), Core::IgnoreAnswers::Keep);
		CHECK(offered.size() == 1);
	}

	SUBCASE("a refresh with a new entry offers the folder again")
	{
		std::ofstream(temp.GetPath() / "b.txt") << "x";
		tree.Refresh(tree.GetRoot(), Core::IgnoreAnswers::Keep);
		CHECK(offered.size() == 2);
		CHECK(offered.back().ends_with(":3"));
	}

	SUBCASE("asking everything again offers the folder again")
	{
		tree.Refresh(tree.GetRoot(), Core::IgnoreAnswers::AskAgain);
		CHECK(offered.size() == 2);
	}

	SUBCASE("an ignored folder's entries are never offered")
	{
		FileNode* skipped = FindChildNamed(tree.GetRoot(), "skipped");
		REQUIRE(skipped != nullptr);
		REQUIRE(skipped->Ignored);
		REQUIRE(tree.Expand(*skipped));
		CHECK(offered.size() == 1);
	}
}
