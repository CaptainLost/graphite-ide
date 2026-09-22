#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <algorithm>

using Core::FileStatus;
using Core::Repository;
using Core::StatusFlags;

namespace {

	Repository OpenAt(const std::filesystem::path& root)
	{
		auto opened = Repository::Open(root);
		REQUIRE(opened.HasValue());
		return std::move(opened).GetValue();
	}

	const FileStatus* Find(const Core::WorkingTreeStatus& status, const char* path)
	{
		const auto entry = std::find_if(status.Entries.begin(), status.Entries.end(),
										[path](const FileStatus& candidate) {
											return candidate.Path == std::filesystem::path(path);
										});

		return entry == status.Entries.end() ? nullptr : &*entry;
	}
}

TEST_CASE("A clean tree reports nothing")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	auto status = repository.QueryStatus();

	REQUIRE(status.HasValue());
	CHECK(status.GetValue().Entries.empty());
}

TEST_CASE("An edited file is modified in the working tree and clean in the index")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");
	fixture.Write("a.txt", "two\n");

	const Repository repository = OpenAt(fixture.GetPath());

	auto status = repository.QueryStatus();

	REQUIRE(status.HasValue());

	const FileStatus* entry = Find(status.GetValue(), "a.txt");

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

	const Repository repository = OpenAt(fixture.GetPath());

	auto status = repository.QueryStatus();

	REQUIRE(status.HasValue());

	const FileStatus* entry = Find(status.GetValue(), "fresh.txt");

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

	const Repository repository = OpenAt(fixture.GetPath());

	auto status = repository.QueryStatus();

	REQUIRE(status.HasValue());

	const FileStatus* entry = Find(status.GetValue(), "a.txt");

	REQUIRE(entry != nullptr);
	CHECK((entry->WorkTree & StatusFlags::Deleted) == StatusFlags::Deleted);
}

TEST_CASE("An ignored file is not listed")
{
	GitFixture fixture;

	fixture.Write(".gitignore", "*.log\n");
	fixture.Commit("add rules");
	fixture.Write("build.log", "noise\n");

	const Repository repository = OpenAt(fixture.GetPath());

	auto status = repository.QueryStatus();

	REQUIRE(status.HasValue());

	// The panel lists what the user can act on. An ignored file is not that, and a build
	// directory would otherwise bury everything else.
	CHECK(Find(status.GetValue(), "build.log") == nullptr);
}

TEST_CASE("Status of a repository with no commits lists the untracked files")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");

	const Repository repository = OpenAt(fixture.GetPath());

	auto status = repository.QueryStatus();

	// An unborn HEAD has no tree to compare against, but the working tree still has
	// files, and this is exactly the state a user is in when they first open a new folder.
	REQUIRE(status.HasValue());

	const FileStatus* entry = Find(status.GetValue(), "a.txt");

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

	const Repository repository = OpenAt(fixture.GetPath());
	const auto status = repository.QueryStatus();
	REQUIRE(status.HasValue());

	const FileStatus* entry = Find(status.GetValue(), "notes");
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

	const Repository repository = OpenAt(fixture.GetPath());
	const auto status = repository.QueryStatus();
	REQUIRE(status.HasValue());

	const FileStatus* entry = Find(status.GetValue(), "loose.txt");
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

	const Repository repository = OpenAt(fixture.GetPath());
	const auto listing = repository.QueryUntrackedIn("notes");
	REQUIRE(listing.HasValue());

	CHECK(listing.GetValue().Entries.size() == 2);
	CHECK(Find(listing.GetValue(), "notes/first.md") != nullptr);
	CHECK(Find(listing.GetValue(), "notes/nested/second.md") != nullptr);
	CHECK(Find(listing.GetValue(), "elsewhere/third.md") == nullptr);
}

TEST_CASE("A nested gitignore keeps its files out of the listing")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("notes/.gitignore", "*.tmp\n");
	fixture.Write("notes/first.md", "one\n");
	fixture.Write("notes/scratch.tmp", "junk\n");

	const Repository repository = OpenAt(fixture.GetPath());
	const auto listing = repository.QueryUntrackedIn("notes");
	REQUIRE(listing.HasValue());

	CHECK(Find(listing.GetValue(), "notes/first.md") != nullptr);
	CHECK(Find(listing.GetValue(), "notes/scratch.tmp") == nullptr);
}

TEST_CASE("A listing of a directory with nothing untracked in it is empty")
{
	GitFixture fixture;

	fixture.Write("tracked/a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());
	const auto listing = repository.QueryUntrackedIn("tracked");
	REQUIRE(listing.HasValue());

	CHECK(listing.GetValue().Entries.empty());
}
