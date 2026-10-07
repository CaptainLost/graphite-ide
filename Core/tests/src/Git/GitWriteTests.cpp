#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

	Core::FileStatus StatusOf(const Core::Repository& repository, const std::string& relativePath)
	{
		const Core::Result<Core::WorkingTreeStatus> statusResult = repository.QueryStatus();
		REQUIRE(statusResult.HasValue());

		const std::filesystem::path wantedPath{relativePath};
		const std::vector<Core::FileStatus>& entries = statusResult.GetValue().Entries;
		for (const Core::FileStatus& entry : entries)
		{
			if (entry.Path == wantedPath)
			{
				return entry;
			}
		}

		const Core::FileStatus cleanFileStatus{};
		return cleanFileStatus;
	}

	bool HasStatusFlag(Core::StatusFlags flags, Core::StatusFlags wantedFlags)
	{
		return (flags & wantedFlags) == wantedFlags;
	}

	// libgit2 caches the index per repository handle, and the application keeps one handle per
	// worker thread, so one handle can act after another has changed the index behind its cache.
	void ReadIndexIntoCache(const Core::Repository& repository)
	{
		REQUIRE(repository.QueryStatus().HasValue());
	}
}

TEST_CASE("Stage adds a new file to the index")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("b.txt", "two\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"b.txt"}).HasValue());

	const Core::FileStatus after = StatusOf(repository, "b.txt");
	CHECK(HasStatusFlag(after.Index, Core::StatusFlags::New));
	CHECK(after.WorkTree == Core::StatusFlags::None);
}

TEST_CASE("Stage adds every file under an untracked directory")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("new folder/b.txt", "two\n");
	fixture.Write("new folder/deep/c.txt", "three\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"new folder"}).HasValue());

	CHECK(HasStatusFlag(StatusOf(repository, "new folder/b.txt").Index, Core::StatusFlags::New));
	CHECK(HasStatusFlag(StatusOf(repository, "new folder/deep/c.txt").Index,
						Core::StatusFlags::New));
}

TEST_CASE("Staging a directory leaves the ignored files under it out")
{
	GitFixture fixture;
	fixture.Write(".gitignore", "*.log\n");
	fixture.Commit("first");
	fixture.Write("dir/b.txt", "two\n");
	fixture.Write("dir/noise.log", "noise\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"dir"}).HasValue());

	CHECK(HasStatusFlag(StatusOf(repository, "dir/b.txt").Index, Core::StatusFlags::New));
	CHECK(StatusOf(repository, "dir/noise.log").Index == Core::StatusFlags::None);
}

TEST_CASE("A directory named like a pattern is staged by its name")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("[ab]/b.txt", "two\n");
	fixture.Write("a/c.txt", "three\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"[ab]"}).HasValue());

	CHECK(HasStatusFlag(StatusOf(repository, "[ab]/b.txt").Index, Core::StatusFlags::New));
	CHECK(StatusOf(repository, "a/c.txt").Index == Core::StatusFlags::None);
}

TEST_CASE("Staging a directory stages only that directory")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("dir/b.txt", "two\n");
	fixture.Write("dir2/c.txt", "three\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"dir"}).HasValue());

	CHECK(HasStatusFlag(StatusOf(repository, "dir/b.txt").Index, Core::StatusFlags::New));
	CHECK(StatusOf(repository, "dir2/c.txt").Index == Core::StatusFlags::None);
}

TEST_CASE("Stage records a modified file")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "changed\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"a.txt"}).HasValue());

	const Core::FileStatus after = StatusOf(repository, "a.txt");
	CHECK(HasStatusFlag(after.Index, Core::StatusFlags::Modified));
	CHECK(after.WorkTree == Core::StatusFlags::None);
}

TEST_CASE("Stage records a deletion through the same call")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Remove("a.txt");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"a.txt"}).HasValue());

	const Core::FileStatus after = StatusOf(repository, "a.txt");
	CHECK(HasStatusFlag(after.Index, Core::StatusFlags::Deleted));
}

TEST_CASE("Unstage puts a staged file back as HEAD has it")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "changed\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"a.txt"}).HasValue());
	REQUIRE(repository.Unstage({"a.txt"}).HasValue());

	const Core::FileStatus after = StatusOf(repository, "a.txt");
	CHECK(after.Index == Core::StatusFlags::None);
	CHECK(HasStatusFlag(after.WorkTree, Core::StatusFlags::Modified));
}

TEST_CASE("Unstage removes the entry when HEAD is unborn")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"a.txt"}).HasValue());
	REQUIRE(repository.Unstage({"a.txt"}).HasValue());

	const Core::FileStatus after = StatusOf(repository, "a.txt");
	CHECK(after.IsUntracked);
}

TEST_CASE("Staging many paths at once writes the index once")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "changed\n");
	fixture.Write("b.txt", "two\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"a.txt", "b.txt"}).HasValue());

	CHECK(HasStatusFlag(StatusOf(repository, "a.txt").Index, Core::StatusFlags::Modified));
	CHECK(HasStatusFlag(StatusOf(repository, "b.txt").Index, Core::StatusFlags::New));
}

TEST_CASE("DiscardWorkTree restores a modified file from the index")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "changed\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.DiscardWorkTree({"a.txt"}).HasValue());

	CHECK(StatusOf(repository, "a.txt").WorkTree == Core::StatusFlags::None);
}

TEST_CASE("DiscardWorkTree leaves untracked files alone")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("new.txt", "fresh\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.DiscardWorkTree({"new.txt"}).HasValue());

	CHECK(std::filesystem::exists(fixture.GetPath() / "new.txt"));
	CHECK(StatusOf(repository, "new.txt").IsUntracked);
}

TEST_CASE("DeleteUntracked removes the file from disk")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("new.txt", "fresh\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.DeleteUntracked({"new.txt"}).HasValue());

	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "new.txt"));
}

TEST_CASE("DeleteUntracked reports a path it cannot remove")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	fixture.Write("folder/inside.txt", "x\n");

	// Windows refuses to remove a file while a handle opened without FILE_SHARE_DELETE holds it,
	// which makes remove_all set an error code.
	std::ofstream handleWithoutShareDelete(fixture.GetPath() / "folder" / "inside.txt",
										   std::ios::app);
	REQUIRE(handleWithoutShareDelete.is_open());

	const Core::Result<void> deleteResult =
		repository.DeleteUntracked({std::filesystem::path("folder")});
	CHECK_FALSE(deleteResult.HasValue());

	handleWithoutShareDelete.close();
}

TEST_CASE("Deleting an untracked directory removes it with everything under it")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("notes/first.md", "one\n");
	fixture.Write("notes/nested/second.md", "two\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.DeleteUntracked({std::filesystem::path("notes")}).HasValue());

	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "notes"));
}

TEST_CASE("Deleting an untracked file still removes just the file")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("loose.txt", "two\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.DeleteUntracked({std::filesystem::path("loose.txt")}).HasValue());

	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "loose.txt"));
	CHECK(std::filesystem::exists(fixture.GetPath() / "a.txt"));
}

TEST_CASE("Commit writes the index as a commit the log then finds")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "changed\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"a.txt"}).HasValue());

	const Core::Result<Core::Oid> commitResult = repository.Commit({.Message = "second"});
	REQUIRE(commitResult.HasValue());

	const Core::Result<Core::LogPage> logResult = repository.QueryLog(Core::LogRequest{});
	REQUIRE(logResult.HasValue());
	REQUIRE_FALSE(logResult.GetValue().Commits.empty());
	CHECK(logResult.GetValue().Commits.front().Id == commitResult.GetValue());
	CHECK(StatusOf(repository, "a.txt").Index == Core::StatusFlags::None);
}

TEST_CASE("Commit refuses an empty message and creates no commit")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "changed\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	REQUIRE(repository.Stage({"a.txt"}).HasValue());

	const Core::Result<Core::LogPage> logBefore = repository.QueryLog(Core::LogRequest{});
	REQUIRE(logBefore.HasValue());

	for (const char* blankMessage : {"", "   ", "\t\r\n  \n"})
	{
		const Core::Result<Core::Oid> commitResult = repository.Commit({.Message = blankMessage});
		REQUIRE_FALSE(commitResult.HasValue());
		CHECK(commitResult.GetError().Code == Core::ErrorCode::InvalidArgument);
	}

	const Core::Result<Core::LogPage> logAfter = repository.QueryLog(Core::LogRequest{});
	REQUIRE(logAfter.HasValue());
	CHECK(logAfter.GetValue().Commits.size() == logBefore.GetValue().Commits.size());
	CHECK(logAfter.GetValue().Commits.front().Id == logBefore.GetValue().Commits.front().Id);
}

TEST_CASE("Commit fails rather than inventing an author it cannot resolve")
{
	GitFixture fixture;
	fixture.SetUnusableIdentity();
	fixture.Write("a.txt", "one\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"a.txt"}).HasValue());

	const Core::Result<Core::Oid> commitResult = repository.Commit({.Message = "first"});
	CHECK_FALSE(commitResult.HasValue());
}

TEST_CASE("Commit into an unborn HEAD makes a root commit")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"a.txt"}).HasValue());

	const Core::Result<Core::Oid> commitResult = repository.Commit({.Message = "first"});
	REQUIRE(commitResult.HasValue());

	const Core::Result<std::optional<Core::Oid>> parentResult =
		repository.FirstParentOf(commitResult.GetValue());
	REQUIRE(parentResult.HasValue());
	CHECK_FALSE(parentResult.GetValue().has_value());
}

TEST_CASE("Commit with nothing staged refuses and creates no commit")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<Core::LogPage> logBefore = repository.QueryLog(Core::LogRequest{});
	REQUIRE(logBefore.HasValue());

	const Core::Result<Core::Oid> commitResult = repository.Commit({.Message = "second"});
	REQUIRE_FALSE(commitResult.HasValue());
	CHECK(commitResult.GetError().Message == "nothing to commit");

	const Core::Result<Core::LogPage> logAfter = repository.QueryLog(Core::LogRequest{});
	REQUIRE(logAfter.HasValue());
	CHECK(logAfter.GetValue().Commits.size() == logBefore.GetValue().Commits.size());
}

TEST_CASE("Amend with an unchanged index still succeeds in rewording the commit")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(repository.Commit({.Message = "first, reworded", .Amend = true}).HasValue());

	const Core::Result<Core::LogPage> logAfter = repository.QueryLog(Core::LogRequest{});
	REQUIRE(logAfter.HasValue());
	CHECK(logAfter.GetValue().Commits.front().Summary == "first, reworded");
}

TEST_CASE("Commit during a merge may keep HEAD's tree")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Oid headId = HeadCommitId(repository);
	const std::string mergeHeadContent = headId.ToHex() + "\n";
	fixture.Write(".git/MERGE_HEAD", mergeHeadContent);

	CHECK(repository.Commit({.Message = "merge"}).HasValue());
}

TEST_CASE("Amend replaces HEAD and keeps its parent")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("b.txt", "two\n");
	fixture.Commit("second");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<Core::LogPage> logBefore = repository.QueryLog(Core::LogRequest{});
	REQUIRE(logBefore.HasValue());
	const std::size_t commitCountBefore = logBefore.GetValue().Commits.size();
	const Core::Oid replacedHeadId = logBefore.GetValue().Commits.front().Id;

	const Core::Result<Core::Oid> amendResult =
		repository.Commit({.Message = "second, reworded", .Amend = true});
	REQUIRE(amendResult.HasValue());
	CHECK(amendResult.GetValue() != replacedHeadId);

	const Core::Result<Core::LogPage> logAfter = repository.QueryLog(Core::LogRequest{});
	REQUIRE(logAfter.HasValue());
	CHECK(logAfter.GetValue().Commits.size() == commitCountBefore);
	CHECK(logAfter.GetValue().Commits.front().Id == amendResult.GetValue());
	CHECK(logAfter.GetValue().Commits.front().Summary == "second, reworded");
}

TEST_CASE("Amend with no message keeps the message it is replacing")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	fixture.Write("b.txt", "two\n");
	REQUIRE(repository.Stage({"b.txt"}).HasValue());

	const Core::Result<Core::Oid> amendResult = repository.Commit({.Message = "", .Amend = true});
	REQUIRE(amendResult.HasValue());

	const Core::Result<Core::LogPage> logAfter = repository.QueryLog(Core::LogRequest{});
	REQUIRE(logAfter.HasValue());
	CHECK(logAfter.GetValue().Commits.front().Summary == "first");
}

TEST_CASE("Amend fails when HEAD is unborn")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	REQUIRE(repository.Stage({"a.txt"}).HasValue());

	const Core::Result<Core::Oid> amendResult =
		repository.Commit({.Message = "nothing to amend", .Amend = true});
	CHECK_FALSE(amendResult.HasValue());
}

TEST_CASE("Staging a conflicted file clears the conflict")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "base\n");
	fixture.Commit("first");
	fixture.Branch("other");
	fixture.Write("a.txt", "mine\n");
	fixture.Commit("mine");
	fixture.Checkout("other");
	fixture.Write("a.txt", "theirs\n");
	fixture.Commit("theirs");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	fixture.StartConflict("master");
	REQUIRE(StatusOf(repository, "a.txt").IsConflicted);

	fixture.Write("a.txt", "resolved\n");
	REQUIRE(repository.Stage({"a.txt"}).HasValue());

	CHECK_FALSE(StatusOf(repository, "a.txt").IsConflicted);
}

TEST_CASE("Commit through a handle that has not seen a stage commits what was staged")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");

	Core::Repository staging = OpenRepositoryAt(fixture.GetPath());
	Core::Repository committing = OpenRepositoryAt(fixture.GetPath());
	ReadIndexIntoCache(committing);

	fixture.Write("x.txt", "new\n");
	REQUIRE(staging.Stage({"x.txt"}).HasValue());

	REQUIRE(committing.Commit({.Message = "add x"}).HasValue());

	const Core::Repository freshHandle = OpenRepositoryAt(fixture.GetPath());
	const Core::FileStatus after = StatusOf(freshHandle, "x.txt");
	CHECK(after.Index == Core::StatusFlags::None);
	CHECK(after.WorkTree == Core::StatusFlags::None);
}

TEST_CASE("Stage through a handle that has not seen an amend does not revert the amended file")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");

	Core::Repository amending = OpenRepositoryAt(fixture.GetPath());
	Core::Repository staging = OpenRepositoryAt(fixture.GetPath());
	ReadIndexIntoCache(staging);

	fixture.Write("a.txt", "two\n");
	REQUIRE(amending.Stage({"a.txt"}).HasValue());
	REQUIRE(amending.Commit({.Message = "", .Amend = true}).HasValue());

	fixture.Write("b.txt", "other\n");
	REQUIRE(staging.Stage({"b.txt"}).HasValue());

	const Core::Repository freshHandle = OpenRepositoryAt(fixture.GetPath());
	CHECK(StatusOf(freshHandle, "a.txt").Index == Core::StatusFlags::None);
	CHECK(HasStatusFlag(StatusOf(freshHandle, "b.txt").Index, Core::StatusFlags::New));
}

TEST_CASE("Unstage through a handle that has not seen a stage keeps that stage")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Write("b.txt", "one\n");
	fixture.Commit("first");

	Core::Repository staging = OpenRepositoryAt(fixture.GetPath());
	Core::Repository unstaging = OpenRepositoryAt(fixture.GetPath());
	ReadIndexIntoCache(unstaging);

	fixture.Write("a.txt", "two\n");
	REQUIRE(staging.Stage({"a.txt"}).HasValue());

	REQUIRE(unstaging.Unstage({"b.txt"}).HasValue());

	const Core::Repository freshHandle = OpenRepositoryAt(fixture.GetPath());
	CHECK(HasStatusFlag(StatusOf(freshHandle, "a.txt").Index, Core::StatusFlags::Modified));
}

TEST_CASE("Discarding through a handle that has not seen a stage restores the staged content")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");

	Core::Repository staging = OpenRepositoryAt(fixture.GetPath());
	Core::Repository discarding = OpenRepositoryAt(fixture.GetPath());
	ReadIndexIntoCache(discarding);

	fixture.Write("a.txt", "staged\n");
	REQUIRE(staging.Stage({"a.txt"}).HasValue());
	fixture.Write("a.txt", "edited\n");

	REQUIRE(discarding.DiscardWorkTree({"a.txt"}).HasValue());

	const std::string content = ReadWithoutCarriageReturns(fixture.GetPath() / "a.txt");
	CHECK(content == "staged\n");
}
