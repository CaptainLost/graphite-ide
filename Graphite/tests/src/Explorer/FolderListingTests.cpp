#include <doctest/doctest.h>

#include "Explorer/FolderListing.h"

#include "DeferredJobs.h"
#include "TempDirectory.h"

#include "Core/Git/Repository.h"
#include "Core/Jobs/CancellationToken.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

using Graphite::FolderListing;
using Graphite::ListedEntry;
using Graphite::ListFolder;

namespace {

	Core::JobSubmitter RunJobsAtOnce()
	{
		return [](std::function<void()> job) { job(); };
	}

	const ListedEntry* FindEntryAt(const std::vector<ListedEntry>& entries, const std::filesystem::path& path)
	{
		for (const ListedEntry& entry : entries)
		{
			if (entry.Path == path)
			{
				return &entry;
			}
		}

		return nullptr;
	}

	std::optional<std::size_t> IndexOfEntryAt(const std::vector<ListedEntry>& entries, const std::filesystem::path& path)
	{
		for (std::size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
		{
			if (entries[entryIndex].Path == path)
			{
				return entryIndex;
			}
		}

		return std::nullopt;
	}

	bool IsListed(const std::vector<ListedEntry>& entries, const std::filesystem::path& path)
	{
		return FindEntryAt(entries, path) != nullptr;
	}

	bool AnyEntryInside(const std::vector<ListedEntry>& entries, std::size_t folderIndex)
	{
		for (const ListedEntry& entry : entries)
		{
			if (entry.Parent == folderIndex)
			{
				return true;
			}
		}

		return false;
	}

	void InitRepository(const TempDirectory& folder, const std::string& ignoreRules)
	{
		const Core::Result<Core::Repository> created = Core::Repository::Init(folder.GetPath());
		REQUIRE(created);

		std::ofstream ignoreFile{folder.GetPath() / ".gitignore", std::ios::binary};
		ignoreFile << ignoreRules;
	}
}

TEST_CASE("ListFolder lists every file and folder under the root, each with the folder holding it")
{
	TempDirectory folder;
	folder.MakeFile("README.md");
	folder.MakeFile("src/Engine/Core.h");
	folder.MakeDirectory("empty");

	const std::vector<ListedEntry> entries = ListFolder(folder.GetPath(), Core::CancellationToken{});

	REQUIRE(entries.size() == 5);

	const ListedEntry* readme = FindEntryAt(entries, folder.GetPath() / "README.md");
	REQUIRE(readme != nullptr);
	CHECK(readme->Name == "README.md");
	CHECK_FALSE(readme->IsDirectory);
	CHECK_FALSE(readme->Parent.has_value());

	const std::optional<std::size_t> source = IndexOfEntryAt(entries, folder.GetPath() / "src");
	const std::optional<std::size_t> engine = IndexOfEntryAt(entries, folder.GetPath() / "src" / "Engine");
	const std::optional<std::size_t> core = IndexOfEntryAt(entries, folder.GetPath() / "src" / "Engine" / "Core.h");
	REQUIRE(source.has_value());
	REQUIRE(engine.has_value());
	REQUIRE(core.has_value());

	CHECK(entries[*source].IsDirectory);
	CHECK(entries[*engine].Parent == source);
	CHECK(entries[*core].Parent == engine);
	CHECK(*source < *engine);
	CHECK(*engine < *core);

	CHECK(IsListed(entries, folder.GetPath() / "empty"));
}

TEST_CASE("ListFolder lists a folder git ignores, marked, without reading what it holds")
{
	TempDirectory folder;
	InitRepository(folder, "bin/\n");
	folder.MakeFile("bin/Debug/Graphite.exe");
	folder.MakeFile("src/Main.cpp");

	const std::vector<ListedEntry> entries = ListFolder(folder.GetPath(), Core::CancellationToken{});

	const std::optional<std::size_t> binaries = IndexOfEntryAt(entries, folder.GetPath() / "bin");
	REQUIRE(binaries.has_value());
	CHECK(entries[*binaries].IsDirectory);
	CHECK(entries[*binaries].Ignored);
	CHECK_FALSE(AnyEntryInside(entries, *binaries));

	const ListedEntry* main = FindEntryAt(entries, folder.GetPath() / "src" / "Main.cpp");
	REQUIRE(main != nullptr);
	CHECK_FALSE(main->Ignored);
}

TEST_CASE("ListFolder marks a file git ignores and still lists it")
{
	TempDirectory folder;
	InitRepository(folder, "*.log\n");
	folder.MakeFile("build.log");

	const std::vector<ListedEntry> entries = ListFolder(folder.GetPath(), Core::CancellationToken{});

	const ListedEntry* log = FindEntryAt(entries, folder.GetPath() / "build.log");
	REQUIRE(log != nullptr);
	CHECK(log->Ignored);
}

TEST_CASE("ListFolder lists .git without reading what it holds")
{
	TempDirectory folder;
	InitRepository(folder, "");

	const std::vector<ListedEntry> entries = ListFolder(folder.GetPath(), Core::CancellationToken{});

	const std::optional<std::size_t> gitFolder = IndexOfEntryAt(entries, folder.GetPath() / ".git");
	REQUIRE(gitFolder.has_value());
	CHECK_FALSE(AnyEntryInside(entries, *gitFolder));
}

TEST_CASE("ListFolder outside a repository reads every folder")
{
	TempDirectory folder;
	folder.MakeFile(".gitignore", "bin/\n");
	folder.MakeFile("bin/Graphite.exe");

	const std::vector<ListedEntry> entries = ListFolder(folder.GetPath(), Core::CancellationToken{});

	const ListedEntry* binaries = FindEntryAt(entries, folder.GetPath() / "bin");
	REQUIRE(binaries != nullptr);
	CHECK_FALSE(binaries->Ignored);
	CHECK(IsListed(entries, folder.GetPath() / "bin" / "Graphite.exe"));
}

TEST_CASE("ListFolder stops once cancelled")
{
	TempDirectory folder;
	folder.MakeFile("a.txt");
	folder.MakeFile("sub/b.txt");

	const Core::CancellationToken cancellation;
	cancellation.Cancel();

	CHECK(ListFolder(folder.GetPath(), cancellation).empty());
}

TEST_CASE("A folder listing reads the folder on its first update")
{
	TempDirectory folder;
	folder.MakeFile("src/Main.cpp");

	FolderListing listing{RunJobsAtOnce()};
	listing.SetRoot(folder.GetPath());

	CHECK_FALSE(listing.HasListing());

	listing.Update();

	CHECK(listing.HasListing());
	CHECK(IsListed(listing.GetEntries(), folder.GetPath() / "src" / "Main.cpp"));
}

TEST_CASE("A folder listing with no change noted does not read the folder again")
{
	TempDirectory folder;
	folder.MakeFile("a.txt");

	FolderListing listing{RunJobsAtOnce()};
	listing.SetRoot(folder.GetPath());
	listing.Update();
	const std::uint64_t versionAfterFirstRead = listing.GetVersion();

	folder.MakeFile("unnoted.txt");
	listing.Update();

	CHECK(listing.GetVersion() == versionAfterFirstRead);
	CHECK_FALSE(IsListed(listing.GetEntries(), folder.GetPath() / "unnoted.txt"));
}

TEST_CASE("A folder listing reads the folder again after a change in a folder it lists")
{
	TempDirectory folder;
	folder.MakeFile("src/a.txt");

	FolderListing listing{RunJobsAtOnce()};
	listing.SetRoot(folder.GetPath());
	listing.Update();
	const std::uint64_t versionAfterFirstRead = listing.GetVersion();

	folder.MakeFile("src/added.txt");
	listing.NoteChangedDirectories({folder.GetPath() / "src"});
	listing.Update();

	CHECK(listing.GetVersion() != versionAfterFirstRead);
	CHECK(IsListed(listing.GetEntries(), folder.GetPath() / "src" / "added.txt"));
}

TEST_CASE("A folder listing does not read the folder again for a change inside a folder git ignores")
{
	TempDirectory folder;
	InitRepository(folder, "bin/\n");
	folder.MakeFile("bin/Debug/Graphite.exe");

	FolderListing listing{RunJobsAtOnce()};
	listing.SetRoot(folder.GetPath());
	listing.Update();

	folder.MakeFile("unnoted.txt");
	listing.NoteChangedDirectories({folder.GetPath() / "bin" / "Debug"});
	listing.Update();

	CHECK_FALSE(IsListed(listing.GetEntries(), folder.GetPath() / "unnoted.txt"));
}

TEST_CASE("A folder listing does not read the folder again for a change inside .git")
{
	TempDirectory folder;
	InitRepository(folder, "");

	FolderListing listing{RunJobsAtOnce()};
	listing.SetRoot(folder.GetPath());
	listing.Update();

	folder.MakeFile("unnoted.txt");
	listing.NoteChangedDirectories({folder.GetPath() / ".git" / "refs"});
	listing.Update();

	CHECK_FALSE(IsListed(listing.GetEntries(), folder.GetPath() / "unnoted.txt"));
}

TEST_CASE("A folder listing reads the folder again when everything changed")
{
	TempDirectory folder;
	folder.MakeFile("a.txt");

	FolderListing listing{RunJobsAtOnce()};
	listing.SetRoot(folder.GetPath());
	listing.Update();

	folder.MakeFile("added.txt");
	listing.NoteEverythingChanged();
	listing.Update();

	CHECK(IsListed(listing.GetEntries(), folder.GetPath() / "added.txt"));
}

TEST_CASE("A folder listing given another root drops the old folder's entries and reads the new one")
{
	TempDirectory first;
	first.MakeFile("first.txt");
	TempDirectory second;
	second.MakeFile("second.txt");

	FolderListing listing{RunJobsAtOnce()};
	listing.SetRoot(first.GetPath());
	listing.Update();

	listing.SetRoot(second.GetPath());

	CHECK_FALSE(listing.HasListing());
	CHECK(listing.GetEntries().empty());

	listing.Update();

	CHECK(IsListed(listing.GetEntries(), second.GetPath() / "second.txt"));
	CHECK_FALSE(IsListed(listing.GetEntries(), first.GetPath() / "first.txt"));
}

TEST_CASE("A folder listing does not take a read of the previous root that finishes after the root changed")
{
	TempDirectory first;
	first.MakeFile("first.txt");
	TempDirectory second;
	second.MakeFile("second.txt");

	DeferredJobs jobs;
	FolderListing listing{jobs.Submitter()};
	listing.SetRoot(first.GetPath());
	listing.Update();

	listing.SetRoot(second.GetPath());
	jobs.RunAll();
	listing.Update();

	CHECK_FALSE(IsListed(listing.GetEntries(), first.GetPath() / "first.txt"));

	jobs.RunAll();
	listing.Update();

	CHECK(IsListed(listing.GetEntries(), second.GetPath() / "second.txt"));
}

TEST_CASE("A folder listing runs one read at a time")
{
	TempDirectory folder;
	folder.MakeFile("a.txt");

	DeferredJobs jobs;
	FolderListing listing{jobs.Submitter()};
	listing.SetRoot(folder.GetPath());

	listing.Update();
	listing.NoteEverythingChanged();
	listing.Update();

	CHECK(jobs.Count() == 1);
	CHECK_FALSE(listing.HasListing());
}

TEST_CASE("A folder listing is up to date only once the read after the last change noted has landed")
{
	TempDirectory folder;
	folder.MakeFile("a.txt");

	DeferredJobs jobs;
	FolderListing listing{jobs.Submitter()};
	listing.SetRoot(folder.GetPath());

	CHECK_FALSE(listing.IsUpToDate());

	listing.Update();
	jobs.RunAll();
	listing.Update();

	CHECK(listing.IsUpToDate());

	listing.NoteChangedDirectories({folder.GetPath()});

	CHECK_FALSE(listing.IsUpToDate());

	listing.Update();

	CHECK_FALSE(listing.IsUpToDate());
	CHECK(listing.HasListing());

	jobs.RunAll();
	listing.Update();

	CHECK(listing.IsUpToDate());
}

TEST_CASE("A folder listing stays up to date through a change inside a folder git ignores")
{
	TempDirectory folder;
	InitRepository(folder, "bin/\n");
	folder.MakeFile("bin/Debug/Graphite.exe");

	FolderListing listing{RunJobsAtOnce()};
	listing.SetRoot(folder.GetPath());
	listing.Update();

	listing.NoteChangedDirectories({folder.GetPath() / "bin" / "Debug"});

	CHECK(listing.IsUpToDate());
}

TEST_CASE("A folder listing reads again after a read that a change overtook")
{
	TempDirectory folder;
	folder.MakeFile("a.txt");

	DeferredJobs jobs;
	FolderListing listing{jobs.Submitter()};
	listing.SetRoot(folder.GetPath());
	listing.Update();
	jobs.RunAll();

	folder.MakeFile("added.txt");
	listing.NoteChangedDirectories({folder.GetPath()});
	listing.Update();

	CHECK(listing.HasListing());
	CHECK_FALSE(IsListed(listing.GetEntries(), folder.GetPath() / "added.txt"));

	jobs.RunAll();
	listing.Update();

	CHECK(IsListed(listing.GetEntries(), folder.GetPath() / "added.txt"));
}
