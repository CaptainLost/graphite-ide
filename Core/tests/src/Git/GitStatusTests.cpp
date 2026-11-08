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

namespace {

	// GitFixture::Commit stages with git_index_add_all, which rejects a nested repository ("invalid path: 'sub/'"), so the
	// superproject's commit is made here: two files by path and the submodule as a gitlink at the inner HEAD.
	void CommitWithSubmodule(const std::filesystem::path& root, const char* submodulePath, const git_oid& submoduleCommit)
	{
		git_repository* repository = nullptr;
		REQUIRE(git_repository_open(&repository, Core::ToUtf8(root).c_str()) == 0);

		git_index* index = nullptr;
		REQUIRE(git_repository_index(&index, repository) == 0);
		REQUIRE(git_index_add_bypath(index, "a.txt") == 0);
		REQUIRE(git_index_add_bypath(index, ".gitmodules") == 0);

		git_index_entry gitlink{};
		gitlink.path = submodulePath;
		gitlink.mode = GIT_FILEMODE_COMMIT;
		gitlink.id = submoduleCommit;
		REQUIRE(git_index_add(index, &gitlink) == 0);
		REQUIRE(git_index_write(index) == 0);

		git_oid treeId{};
		REQUIRE(git_index_write_tree(&treeId, index) == 0);

		git_tree* tree = nullptr;
		REQUIRE(git_tree_lookup(&tree, repository, &treeId) == 0);

		git_signature* signature = nullptr;
		REQUIRE(git_signature_new(&signature, GitFixture::kDefaultAuthorName, GitFixture::kDefaultAuthorEmail, 1000000000, 0) == 0);

		git_oid commitId{};
		REQUIRE(git_commit_create(&commitId, repository, "HEAD", signature, signature, "UTF-8", "add a and the submodule", tree, 0,
								  nullptr) == 0);

		git_signature_free(signature);
		git_tree_free(tree);
		git_index_free(index);
		git_repository_free(repository);
	}
}

TEST_CASE("Status: skipping submodules leaves a changed submodule out")
{
	GitFixture inner;
	inner.Write("lib.txt", "one\n");
	inner.Commit("add lib");

	git_oid innerHead{};
	{
		git_repository* innerRepository = nullptr;
		REQUIRE(git_repository_open(&innerRepository, Core::ToUtf8(inner.GetPath()).c_str()) == 0);
		REQUIRE(git_reference_name_to_id(&innerHead, innerRepository, "HEAD") == 0);
		git_repository_free(innerRepository);
	}

	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Write(".gitmodules", "[submodule \"sub\"]\n\tpath = sub\n\turl = ../inner\n");

	std::error_code copyError;
	std::filesystem::copy(inner.GetPath(), fixture.GetPath() / "sub",
						  std::filesystem::copy_options::recursive | std::filesystem::copy_options::copy_symlinks, copyError);
	REQUIRE_FALSE(copyError);

	CommitWithSubmodule(fixture.GetPath(), "sub", innerHead);

	fixture.Write("sub/lib.txt", "two\n");
	fixture.Write("a.txt", "two\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<WorkingTreeStatus> walked = repository.QueryStatus();
	REQUIRE(walked.HasValue());
	CHECK(FindEntry(walked.GetValue(), "sub") != nullptr);
	CHECK(FindEntry(walked.GetValue(), "a.txt") != nullptr);

	const Result<WorkingTreeStatus> skipped = repository.QueryStatus(Core::SubmoduleWalk::Skipped);
	REQUIRE(skipped.HasValue());
	CHECK(FindEntry(skipped.GetValue(), "sub") == nullptr);
	CHECK(FindEntry(skipped.GetValue(), "a.txt") != nullptr);
}
