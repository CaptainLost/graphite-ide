#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <string>

using Core::LogRequest;
using Core::Repository;

TEST_CASE("A file is read back exactly as it was committed")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\ntwo\nthree\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<std::string> text = repository.ReadBlob("a.txt", HeadCommitId(repository));

	REQUIRE(text.HasValue());
	CHECK(text.GetValue() == "one\ntwo\nthree\n");
}

TEST_CASE("A file is read at the revision asked for, not the newest")
{
	GitFixture fixture;

	fixture.Write("a.txt", "first\n");
	const std::string first = fixture.Commit("add a");

	fixture.Write("a.txt", "second\n");
	fixture.Commit("change a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	// Taken from the log rather than parsed back out of the fixture's hex, which would put a
	// second hex reader in the tests.
	Core::Result<Core::LogPage> log = repository.QueryLog(LogRequest{});
	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 2);
	REQUIRE(log.GetValue().Commits[1].Id.ToHex() == first);

	const Core::Oid olderCommitId = log.GetValue().Commits[1].Id;
	Core::Result<std::string> text = repository.ReadBlob("a.txt", olderCommitId);

	REQUIRE(text.HasValue());
	CHECK(text.GetValue() == "first\n");
}

TEST_CASE("A file with no trailing newline keeps its last line")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\ntwo");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<std::string> text = repository.ReadBlob("a.txt", HeadCommitId(repository));

	REQUIRE(text.HasValue());
	CHECK(text.GetValue() == "one\ntwo");
}

TEST_CASE("Binary content survives the round trip without stopping at a zero byte")
{
	GitFixture fixture;

	const std::string bytes("\x00\x01\x02\x00\x03", 5);
	fixture.Write("data.bin", bytes);
	fixture.Commit("add binary");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<std::string> text = repository.ReadBlob("data.bin", HeadCommitId(repository));

	REQUIRE(text.HasValue());
	CHECK(text.GetValue().size() == 5);
	CHECK(text.GetValue() == bytes);
}

TEST_CASE("An empty file reads back as an empty string, though libgit2 may hand back a null content pointer for it")
{
	GitFixture fixture;

	fixture.Write("empty.txt", "");
	fixture.Commit("add an empty file");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<std::string> text = repository.ReadBlob("empty.txt", HeadCommitId(repository));

	REQUIRE(text.HasValue());
	CHECK(text.GetValue().empty());
}

TEST_CASE("A path that is not in that commit is NotFound, the answer the diff view expects for an added file's earlier side")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<std::string> text = repository.ReadBlob("missing.txt", HeadCommitId(repository));

	REQUIRE_FALSE(text.HasValue());
	CHECK(text.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("A directory is not a file, and is reported as NotFound exactly like a missing file")
{
	GitFixture fixture;

	fixture.Write("dir/a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<std::string> text = repository.ReadBlob("dir", HeadCommitId(repository));

	REQUIRE_FALSE(text.HasValue());
	CHECK(text.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("A file inside a directory is found by its full path")
{
	GitFixture fixture;

	fixture.Write("dir/nested/a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<std::string> text = repository.ReadBlob("dir/nested/a.txt", HeadCommitId(repository));

	REQUIRE(text.HasValue());
	CHECK(text.GetValue() == "one\n");
}

TEST_CASE("A backslash-separated path finds the same file")
{
	GitFixture fixture;

	fixture.Write("dir/a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	// Only on Windows does the / operator join with a backslash, while git speaks forward
	// slashes on every platform.
	const std::filesystem::path nativeSeparatedPath = std::filesystem::path("dir") / "a.txt";
	Core::Result<std::string> text = repository.ReadBlob(nativeSeparatedPath, HeadCommitId(repository));

	REQUIRE(text.HasValue());
	CHECK(text.GetValue() == "one\n");
}

TEST_CASE("A commit with a parent reports it")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("add a");

	fixture.Write("a.txt", "two\n");
	fixture.Commit("change a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<std::optional<Core::Oid>> parent = repository.FirstParentOf(HeadCommitId(repository));

	REQUIRE(parent.HasValue());
	REQUIRE(parent.GetValue().has_value());
	CHECK(parent.GetValue()->ToHex() == first);
}

TEST_CASE("A root commit reports no parent")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<std::optional<Core::Oid>> parent = repository.FirstParentOf(HeadCommitId(repository));

	REQUIRE(parent.HasValue());
	CHECK_FALSE(parent.GetValue().has_value());
}

TEST_CASE("A merge commit reports its first parent")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string mainline = fixture.Commit("add a");

	fixture.Branch("other");
	fixture.Checkout("other");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b on other");

	fixture.Checkout("master");

	const std::string merge = fixture.MergeCommit("merge other", "other");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Result<Core::LogPage> log = repository.QueryLog(LogRequest{});
	REQUIRE(log.HasValue());

	Core::Oid mergeId;
	for (const Core::CommitSummary& commit : log.GetValue().Commits)
	{
		if (commit.Id.ToHex() == merge)
		{
			mergeId = commit.Id;
		}
	}

	Core::Result<std::optional<Core::Oid>> parent = repository.FirstParentOf(mergeId);

	REQUIRE(parent.HasValue());
	REQUIRE(parent.GetValue().has_value());
	CHECK(parent.GetValue()->ToHex() == mainline);
}

TEST_CASE("The index side of a file is read as it was staged")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");

	Repository repository = OpenRepositoryAt(fixture.GetPath());

	fixture.Write("a.txt", "staged\n");
	REQUIRE(repository.Stage({"a.txt"}).HasValue());
	fixture.Write("a.txt", "edited again\n");

	const Core::Result<std::string> staged = repository.ReadIndexBlob("a.txt");
	REQUIRE(staged.HasValue());
	CHECK(staged.GetValue() == "staged\n");
}

TEST_CASE("An empty staged file is read as empty text")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");

	Repository repository = OpenRepositoryAt(fixture.GetPath());

	fixture.Write("empty.txt", "");
	REQUIRE(repository.Stage({"empty.txt"}).HasValue());

	const Core::Result<std::string> staged = repository.ReadIndexBlob("empty.txt");
	REQUIRE(staged.HasValue());
	CHECK(staged.GetValue().empty());
}

TEST_CASE("The index has nothing to say about a file that was never staged")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");
	fixture.Write("new.txt", "untracked\n");

	Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<std::string> staged = repository.ReadIndexBlob("new.txt");
	REQUIRE_FALSE(staged.HasValue());
	CHECK(staged.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("The working-tree side of a file is read as it stands on disk")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");

	Repository repository = OpenRepositoryAt(fixture.GetPath());

	fixture.Write("a.txt", "edited but not staged\n");

	const Core::Result<std::string> onDisk = repository.ReadWorkingTreeFile("a.txt");
	REQUIRE(onDisk.HasValue());
	CHECK(onDisk.GetValue() == "edited but not staged\n");
}

TEST_CASE("A working-tree file that is gone answers NotFound rather than failing")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");
	fixture.Remove("a.txt");

	Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<std::string> onDisk = repository.ReadWorkingTreeFile("a.txt");
	REQUIRE_FALSE(onDisk.HasValue());
	CHECK(onDisk.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("A working-tree file too large to lex is refused before it is read")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");

	Repository repository = OpenRepositoryAt(fixture.GetPath());

	fixture.Write("huge.txt", std::string(Core::MaximumDiffTextSize + 1, 'x'));

	const Core::Result<std::string> onDisk = repository.ReadWorkingTreeFile("huge.txt");
	REQUIRE_FALSE(onDisk.HasValue());
	CHECK(onDisk.GetError().Code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("HEAD names the commit the log puts first")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "two\n");
	fixture.Commit("second");

	Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<Core::Oid> head = repository.HeadCommit();
	REQUIRE(head.HasValue());
	CHECK(head.GetValue() == HeadCommitId(repository));
}

TEST_CASE("An unborn HEAD names no commit")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");

	Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<Core::Oid> head = repository.HeadCommit();
	REQUIRE_FALSE(head.HasValue());
	CHECK(head.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("The index side is read from the index on disk, not from one this handle cached earlier")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");

	Repository stagingHandle = OpenRepositoryAt(fixture.GetPath());
	Repository readingHandleCachedBeforeStage = OpenRepositoryAt(fixture.GetPath());
	REQUIRE(readingHandleCachedBeforeStage.ReadIndexBlob("a.txt").HasValue());

	fixture.Write("a.txt", "staged\n");
	REQUIRE(stagingHandle.Stage({"a.txt"}).HasValue());

	const Core::Result<std::string> staged = readingHandleCachedBeforeStage.ReadIndexBlob("a.txt");
	REQUIRE(staged.HasValue());
	CHECK(staged.GetValue() == "staged\n");
}
