#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <string>

using Core::LogRequest;
using Core::Repository;

namespace {

	Repository OpenAt(const std::filesystem::path& root)
	{
		auto opened = Repository::Open(root);
		REQUIRE(opened.HasValue());
		return std::move(opened).GetValue();
	}

	Core::Oid HeadId(const Repository& repository)
	{
		auto log = repository.QueryLog(LogRequest{});
		REQUIRE(log.HasValue());
		REQUIRE_FALSE(log.GetValue().Commits.empty());
		return log.GetValue().Commits.front().Id;
	}
}

TEST_CASE("A file is read back exactly as it was committed")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\ntwo\nthree\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	auto text = repository.ReadBlob("a.txt", HeadId(repository));

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

	const Repository repository = OpenAt(fixture.GetPath());

	// The log comes back newest first, so the second entry is the commit the fixture made
	// first — taken from the query rather than parsed back out of the hex the fixture
	// returned, which would put a second hex reader in the tests.
	auto log = repository.QueryLog(LogRequest{});
	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 2);
	REQUIRE(log.GetValue().Commits[1].Id.ToHex() == first);

	auto text = repository.ReadBlob("a.txt", log.GetValue().Commits[1].Id);

	// The whole point of the function: the diff view needs the file as it stood at one
	// commit, which is never the file on disk.
	REQUIRE(text.HasValue());
	CHECK(text.GetValue() == "first\n");
}

TEST_CASE("A file with no trailing newline keeps its last line")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\ntwo");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	auto text = repository.ReadBlob("a.txt", HeadId(repository));

	REQUIRE(text.HasValue());
	CHECK(text.GetValue() == "one\ntwo");
}

TEST_CASE("Binary content survives the round trip")
{
	GitFixture fixture;

	const std::string bytes("\x00\x01\x02\x00\x03", 5);
	fixture.Write("data.bin", bytes);
	fixture.Commit("add binary");

	const Repository repository = OpenAt(fixture.GetPath());

	auto text = repository.ReadBlob("data.bin", HeadId(repository));

	// A std::string holds bytes, and nothing here may stop at the first zero — the caller
	// decides what to do with a file that is not text.
	REQUIRE(text.HasValue());
	CHECK(text.GetValue().size() == 5);
	CHECK(text.GetValue() == bytes);
}

TEST_CASE("An empty file reads back as an empty string")
{
	GitFixture fixture;

	// An empty write still creates the file rather than skipping it — MakeFile opens with
	// truncate and streams the (empty) contents, so the commit genuinely has this path.
	fixture.Write("empty.txt", "");
	fixture.Commit("add an empty file");

	const Repository repository = OpenAt(fixture.GetPath());

	auto text = repository.ReadBlob("empty.txt", HeadId(repository));

	// The blob for a zero-byte file may hand back a null content pointer from libgit2;
	// this proves the read succeeds rather than hitting undefined behaviour on that path.
	REQUIRE(text.HasValue());
	CHECK(text.GetValue().empty());
}

TEST_CASE("A path that is not in that commit is not found")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	auto text = repository.ReadBlob("missing.txt", HeadId(repository));

	// Not an error state the user caused: a file added later is genuinely absent from an
	// earlier commit, and the diff view asks about exactly that when it shows an added file.
	REQUIRE_FALSE(text.HasValue());
	CHECK(text.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("A directory is not a file")
{
	GitFixture fixture;

	fixture.Write("dir/a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	auto text = repository.ReadBlob("dir", HeadId(repository));

	// Pinned to NotFound, not merely "not a value": the next task treats NotFound as "the
	// file did not exist on that side," so any other code would read as a broken repository.
	REQUIRE_FALSE(text.HasValue());
	CHECK(text.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("A file inside a directory is found by its full path")
{
	GitFixture fixture;

	fixture.Write("dir/nested/a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	auto text = repository.ReadBlob("dir/nested/a.txt", HeadId(repository));

	REQUIRE(text.HasValue());
	CHECK(text.GetValue() == "one\n");
}

TEST_CASE("A backslash-separated path finds the same file")
{
	GitFixture fixture;

	fixture.Write("dir/a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	// Every path in this application arrives as a std::filesystem::path, which on Windows
	// carries native separators, while git speaks forward slashes on every platform.
	auto text = repository.ReadBlob(std::filesystem::path("dir") / "a.txt", HeadId(repository));

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

	const Repository repository = OpenAt(fixture.GetPath());

	auto parent = repository.FirstParentOf(HeadId(repository));

	REQUIRE(parent.HasValue());
	REQUIRE(parent.GetValue().has_value());
	CHECK(parent.GetValue()->ToHex() == first);
}

TEST_CASE("A root commit reports no parent")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	auto parent = repository.FirstParentOf(HeadId(repository));

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

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});
	REQUIRE(log.HasValue());

	Core::Oid mergeId;
	for (const auto& entry : log.GetValue().Commits)
	{
		if (entry.Id.ToHex() == merge)
		{
			mergeId = entry.Id;
		}
	}

	// A merge is shown against the first parent only — the other parents are offered
	// separately, so this is the side its diff is taken against.
	auto parent = repository.FirstParentOf(mergeId);

	REQUIRE(parent.HasValue());
	REQUIRE(parent.GetValue().has_value());
	CHECK(parent.GetValue()->ToHex() == mainline);
}

TEST_CASE("The index side of a file is read as it was staged")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");

	Repository repository = OpenAt(fixture.GetPath());

	fixture.Write("a.txt", "staged\n");
	REQUIRE(repository.Stage({"a.txt"}).HasValue());
	fixture.Write("a.txt", "edited again\n");

	const auto staged = repository.ReadIndexBlob("a.txt");
	REQUIRE(staged.HasValue());
	CHECK(staged.GetValue() == "staged\n");
}

TEST_CASE("An empty staged file is read as empty text")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");

	Repository repository = OpenAt(fixture.GetPath());

	fixture.Write("empty.txt", "");
	REQUIRE(repository.Stage({"empty.txt"}).HasValue());

	const auto staged = repository.ReadIndexBlob("empty.txt");
	REQUIRE(staged.HasValue());
	CHECK(staged.GetValue().empty());
}

TEST_CASE("The index has nothing to say about a file that was never staged")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");
	fixture.Write("new.txt", "untracked\n");

	Repository repository = OpenAt(fixture.GetPath());

	const auto staged = repository.ReadIndexBlob("new.txt");
	REQUIRE_FALSE(staged.HasValue());
	CHECK(staged.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("The working-tree side of a file is read as it stands on disk")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");

	Repository repository = OpenAt(fixture.GetPath());

	fixture.Write("a.txt", "edited but not staged\n");

	const auto onDisk = repository.ReadWorkingTreeFile("a.txt");
	REQUIRE(onDisk.HasValue());
	CHECK(onDisk.GetValue() == "edited but not staged\n");
}

TEST_CASE("A working-tree file that is gone answers NotFound rather than failing")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");
	fixture.Remove("a.txt");

	Repository repository = OpenAt(fixture.GetPath());

	const auto onDisk = repository.ReadWorkingTreeFile("a.txt");
	REQUIRE_FALSE(onDisk.HasValue());
	CHECK(onDisk.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("A working-tree file too large to lex is refused before it is read")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");

	Repository repository = OpenAt(fixture.GetPath());

	fixture.Write("huge.txt", std::string(Core::MaximumDiffTextSize + 1, 'x'));

	const auto onDisk = repository.ReadWorkingTreeFile("huge.txt");
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

	Repository repository = OpenAt(fixture.GetPath());

	const auto head = repository.HeadCommit();
	REQUIRE(head.HasValue());
	CHECK(head.GetValue() == HeadId(repository));
}

TEST_CASE("An unborn HEAD names no commit")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");

	Repository repository = OpenAt(fixture.GetPath());

	const auto head = repository.HeadCommit();
	REQUIRE_FALSE(head.HasValue());
	CHECK(head.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("The index side is read from the index on disk, not from one this handle cached earlier")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");

	// Two handles on one repository, as the application has one per worker thread. The reader
	// loads the index first, so its cache predates the stage.
	Repository staging = OpenAt(fixture.GetPath());
	Repository reading = OpenAt(fixture.GetPath());
	REQUIRE(reading.ReadIndexBlob("a.txt").HasValue());

	fixture.Write("a.txt", "staged\n");
	REQUIRE(staging.Stage({"a.txt"}).HasValue());

	const auto staged = reading.ReadIndexBlob("a.txt");
	REQUIRE(staged.HasValue());
	CHECK(staged.GetValue() == "staged\n");
}
