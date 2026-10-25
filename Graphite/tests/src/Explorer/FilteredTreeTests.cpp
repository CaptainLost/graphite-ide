#include <doctest/doctest.h>

#include "Explorer/FilteredTree.h"
#include "Explorer/FolderListing.h"

#include "Core/FileSystem/FileTree.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using Graphite::BuildFilteredTree;
using Graphite::ListedEntry;

namespace {

	const std::filesystem::path kRoot = "D:/Work/proj";

	class ListingBuilder
	{
	public:
		std::size_t AddFolder(const std::string& name, std::optional<std::size_t> parent = std::nullopt)
		{
			return Add(name, parent, EntryKind::Folder);
		}

		std::size_t AddFile(const std::string& name, std::optional<std::size_t> parent = std::nullopt)
		{
			return Add(name, parent, EntryKind::File);
		}

		void MarkIgnored(std::size_t entryIndex) { m_Entries[entryIndex].Ignored = true; }

		const std::vector<ListedEntry>& GetEntries() const { return m_Entries; }

	private:
		enum class EntryKind
		{
			File,
			Folder
		};

		std::size_t Add(const std::string& name, std::optional<std::size_t> parent, EntryKind kind)
		{
			const std::filesystem::path folder = parent.has_value() ? m_Entries[*parent].Path : kRoot;

			m_Entries.push_back(ListedEntry{
				.Path = folder / name,
				.Name = name,
				.IsDirectory = kind == EntryKind::Folder,
				.Parent = parent,
			});

			return m_Entries.size() - 1;
		}

		std::vector<ListedEntry> m_Entries;
	};

	Core::FileNode RootNode()
	{
		return Core::FileNode{.Path = kRoot, .Name = "proj", .IsDirectory = true};
	}

	bool NameContains(const ListedEntry& entry, const std::string& text)
	{
		return entry.Name.find(text) != std::string::npos;
	}

	Core::FileNode FilterBy(const std::vector<ListedEntry>& entries, const std::string& text)
	{
		return BuildFilteredTree(RootNode(), entries,
								 [&text](const ListedEntry& entry) { return NameContains(entry, text); });
	}

	std::vector<std::string> NamesOf(const std::vector<Core::FileNode>& nodes)
	{
		std::vector<std::string> names;

		for (const Core::FileNode& node : nodes)
		{
			names.push_back(node.Name);
		}

		return names;
	}
}

TEST_CASE("A match inside folders nobody opened is shown with every folder on the way to it, open")
{
	ListingBuilder listing;
	const std::size_t source = listing.AddFolder("src");
	const std::size_t engine = listing.AddFolder("Engine", source);
	listing.AddFile("Main.cpp", engine);

	const Core::FileNode root = FilterBy(listing.GetEntries(), "Main");

	CHECK(root.Path == kRoot);
	CHECK(root.Expanded);
	REQUIRE(root.Children.size() == 1);

	const Core::FileNode& sourceNode = root.Children[0];
	CHECK(sourceNode.Name == "src");
	CHECK(sourceNode.Path == kRoot / "src");
	CHECK(sourceNode.IsDirectory);
	CHECK(sourceNode.Expanded);
	CHECK(sourceNode.ChildrenLoaded);
	REQUIRE(sourceNode.Children.size() == 1);

	const Core::FileNode& engineNode = sourceNode.Children[0];
	CHECK(engineNode.Name == "Engine");
	CHECK(engineNode.Expanded);
	REQUIRE(engineNode.Children.size() == 1);

	const Core::FileNode& mainNode = engineNode.Children[0];
	CHECK(mainNode.Name == "Main.cpp");
	CHECK(mainNode.Path == kRoot / "src" / "Engine" / "Main.cpp");
	CHECK_FALSE(mainNode.IsDirectory);
}

TEST_CASE("Files and folders that lead to no match are left out of the filtered tree")
{
	ListingBuilder listing;
	const std::size_t source = listing.AddFolder("src");
	listing.AddFile("Main.cpp", source);
	listing.AddFile("Other.cpp", source);
	const std::size_t documentation = listing.AddFolder("docs");
	listing.AddFile("Guide.md", documentation);
	listing.AddFile("README.md");

	const Core::FileNode root = FilterBy(listing.GetEntries(), "Main");

	REQUIRE(root.Children.size() == 1);
	CHECK(root.Children[0].Name == "src");
	CHECK(NamesOf(root.Children[0].Children) == std::vector<std::string>{"Main.cpp"});
}

TEST_CASE("A folder that matches by its own name is shown closed, without the content that does not match")
{
	ListingBuilder listing;
	const std::size_t build = listing.AddFolder("build");
	listing.AddFile("output.txt", build);

	const Core::FileNode root = FilterBy(listing.GetEntries(), "build");

	REQUIRE(root.Children.size() == 1);

	const Core::FileNode& buildNode = root.Children[0];
	CHECK(buildNode.Name == "build");
	CHECK(buildNode.IsDirectory);
	CHECK(buildNode.ChildrenLoaded);
	CHECK_FALSE(buildNode.Expanded);
	CHECK(buildNode.Children.empty());
}

TEST_CASE("A folder that matches by its own name and holds a match is open and shows that match")
{
	ListingBuilder listing;
	const std::size_t tests = listing.AddFolder("tests");
	listing.AddFile("FilterTests.cpp", tests);
	listing.AddFile("Main.cpp", tests);

	const Core::FileNode root = FilterBy(listing.GetEntries(), "ests");

	REQUIRE(root.Children.size() == 1);
	CHECK(root.Children[0].Expanded);
	CHECK(NamesOf(root.Children[0].Children) == std::vector<std::string>{"FilterTests.cpp"});
}

TEST_CASE("The filtered tree lists folders before files and names in the file tree's order")
{
	ListingBuilder listing;
	listing.AddFile("b_match.txt");
	const std::size_t secondFolder = listing.AddFolder("z_match_folder");
	listing.AddFile("inner_match.txt", secondFolder);
	listing.AddFile("a_match.txt");
	listing.AddFolder("c_match_folder");

	const Core::FileNode root = FilterBy(listing.GetEntries(), "match");

	CHECK(NamesOf(root.Children) ==
		  std::vector<std::string>{"c_match_folder", "z_match_folder", "a_match.txt", "b_match.txt"});
}

TEST_CASE("An ignored entry keeps its ignored mark in the filtered tree")
{
	ListingBuilder listing;
	const std::size_t binaries = listing.AddFolder("bin");
	listing.MarkIgnored(binaries);
	const std::size_t log = listing.AddFile("build.log");
	listing.MarkIgnored(log);
	listing.AddFile("build.cpp");

	const Core::FileNode root = FilterBy(listing.GetEntries(), "b");

	REQUIRE(root.Children.size() == 3);
	CHECK(root.Children[0].Name == "bin");
	CHECK(root.Children[0].Ignored);
	CHECK(root.Children[1].Name == "build.cpp");
	CHECK_FALSE(root.Children[1].Ignored);
	CHECK(root.Children[2].Name == "build.log");
	CHECK(root.Children[2].Ignored);
}

TEST_CASE("With nothing matching the filtered tree is the root alone")
{
	ListingBuilder listing;
	const std::size_t source = listing.AddFolder("src");
	listing.AddFile("Main.cpp", source);

	const Core::FileNode root = FilterBy(listing.GetEntries(), "nothing like this");

	CHECK(root.Path == kRoot);
	CHECK(root.Name == "proj");
	CHECK(root.Children.empty());
}

TEST_CASE("A filter that matches nothing says so outright only once the file list is up to date")
{
	CHECK(Graphite::NoMatchingFilesText("Foo", Graphite::ListingFreshness::UpToDate) == "No files match \"Foo\"");
	CHECK(Graphite::NoMatchingFilesText("Foo", Graphite::ListingFreshness::Refreshing) ==
		  "No files match \"Foo\" yet \xC2\xB7 refreshing the file list");
}
