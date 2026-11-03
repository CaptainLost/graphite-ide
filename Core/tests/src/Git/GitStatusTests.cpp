#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <filesystem>

using Core::FileStatus;
using Core::Repository;
using Core::Result;
using Core::StatusFlags;
using Core::WorkingTreeStatus;

namespace {

	const FileStatus* FindEntry(const WorkingTreeStatus& status, const char* path)
	{
		for (const FileStatus& candidate : status.Entries)
		{
			if (candidate.Path == std::filesystem::path(path))
			{
				return &candidate;
			}
		}

		return nullptr;
	}
}

TEST_CASE("A clean tree reports nothing")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<WorkingTreeStatus> status = repository.QueryStatus();

	REQUIRE(status.HasValue());
	CHECK(status.GetValue().Entries.empty());
}

TEST_CASE("An edited file is modified in the working tree and clean in the index")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");
	fixture.Write("a.txt", "two\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<WorkingTreeStatus> status = repository.QueryStatus();

	REQUIRE(status.HasValue());

	const FileStatus* entry = FindEntry(status.GetValue(), "a.txt");

	REQUIRE(entry != nullptr);
	CHECK((entry->WorkTree & StatusFlags::Modified) == StatusFlags::Modified);
	CHECK(entry->Index == StatusFlags::None);
	CHECK_FALSE(entry->IsUntracked);
}

TEST_CASE("A file that was never added is untracked")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");
	fixture.Write("fresh.txt", "new\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<WorkingTreeStatus> status = repository.QueryStatus();

	REQUIRE(status.HasValue());

	const FileStatus* entry = FindEntry(status.GetValue(), "fresh.txt");

	REQUIRE(entry != nullptr);
	CHECK(entry->IsUntracked);
	CHECK((entry->WorkTree & StatusFlags::New) == StatusFlags::New);
}

TEST_CASE("A deleted file is reported as deleted rather than dropped")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");
	fixture.Remove("a.txt");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<WorkingTreeStatus> status = repository.QueryStatus();

	REQUIRE(status.HasValue());

	const FileStatus* entry = FindEntry(status.GetValue(), "a.txt");

	REQUIRE(entry != nullptr);
	CHECK((entry->WorkTree & StatusFlags::Deleted) == StatusFlags::Deleted);
}

TEST_CASE("An ignored file is not listed so a build directory cannot bury everything the user can act on")
{
	GitFixture fixture;

	fixture.Write(".gitignore", "*.log\n");
	fixture.Commit("add rules");
	fixture.Write("build.log", "noise\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<WorkingTreeStatus> status = repository.QueryStatus();

	REQUIRE(status.HasValue());

	CHECK(FindEntry(status.GetValue(), "build.log") == nullptr);
}

TEST_CASE("Status of a repository with no commits lists the untracked files although an unborn HEAD has no tree to compare against")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<WorkingTreeStatus> status = repository.QueryStatus();

	REQUIRE(status.HasValue());

	const FileStatus* entry = FindEntry(status.GetValue(), "a.txt");

	REQUIRE(entry != nullptr);
	CHECK(entry->IsUntracked);
}

TEST_CASE("An untracked directory is reported as a directory without a trailing separator")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("notes/first.md", "one\n");
	fixture.Write("notes/second.md", "two\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Result<WorkingTreeStatus> status = repository.QueryStatus();
	REQUIRE(status.HasValue());

	const FileStatus* entry = FindEntry(status.GetValue(), "notes");
	REQUIRE(entry != nullptr);

	CHECK(entry->IsDirectory);
	CHECK(entry->IsUntracked);
	CHECK(entry->Path.filename() == std::filesystem::path("notes"));
	CHECK(entry->Path.parent_path().empty());
}

TEST_CASE("An untracked file is not a directory")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("loose.txt", "two\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Result<WorkingTreeStatus> status = repository.QueryStatus();
	REQUIRE(status.HasValue());

	const FileStatus* entry = FindEntry(status.GetValue(), "loose.txt");
	REQUIRE(entry != nullptr);

	CHECK_FALSE(entry->IsDirectory);
}

TEST_CASE("An untracked directory expands to the files under it")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("notes/first.md", "one\n");
	fixture.Write("notes/nested/second.md", "two\n");
	fixture.Write("elsewhere/third.md", "three\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Result<WorkingTreeStatus> listing = repository.QueryUntrackedIn("notes");
	REQUIRE(listing.HasValue());

	CHECK(listing.GetValue().Entries.size() == 2);
	CHECK(FindEntry(listing.GetValue(), "notes/first.md") != nullptr);
	CHECK(FindEntry(listing.GetValue(), "notes/nested/second.md") != nullptr);
	CHECK(FindEntry(listing.GetValue(), "elsewhere/third.md") == nullptr);
}

TEST_CASE("A nested gitignore keeps its files out of the listing")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("notes/.gitignore", "*.tmp\n");
	fixture.Write("notes/first.md", "one\n");
	fixture.Write("notes/scratch.tmp", "junk\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Result<WorkingTreeStatus> listing = repository.QueryUntrackedIn("notes");
	REQUIRE(listing.HasValue());

	CHECK(FindEntry(listing.GetValue(), "notes/first.md") != nullptr);
	CHECK(FindEntry(listing.GetValue(), "notes/scratch.tmp") == nullptr);
}

TEST_CASE("A listing of a directory with nothing untracked in it is empty")
{
	GitFixture fixture;

	fixture.Write("tracked/a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Result<WorkingTreeStatus> listing = repository.QueryUntrackedIn("tracked");
	REQUIRE(listing.HasValue());

	CHECK(listing.GetValue().Entries.empty());
}

TEST_CASE("A directory name with bracket characters is matched literally, not as a glob")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("notes[1]/first.md", "one\n");
	fixture.Write("notes[1]/second.md", "two\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Result<WorkingTreeStatus> listing = repository.QueryUntrackedIn("notes[1]");
	REQUIRE(listing.HasValue());

	CHECK(FindEntry(listing.GetValue(), "notes[1]/first.md") != nullptr);
	CHECK(FindEntry(listing.GetValue(), "notes[1]/second.md") != nullptr);
}
