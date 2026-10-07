#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <git2.h>

#include <cstddef>
#include <cstdint>
#include <string>

using Core::DiffRequest;
using Core::DiffTarget;
using Core::FileChange;
using Core::Repository;

namespace {

	Core::Oid IdOfNoStoredObject()
	{
		const std::uint8_t kFillerByte = 0xAB;
		Core::Oid missing;
		missing.Bytes.fill(kFillerByte);
		return missing;
	}
}

TEST_CASE("A commit diffs against its parent")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\ntwo\nthree\n");
	fixture.Commit("add a");
	fixture.Write("a.txt", "one\nTWO\nthree\n");
	fixture.Commit("shout");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::CommitToParent;
	request.From = HeadCommitId(repository);

	const Core::Result<Core::Diff> diff = repository.QueryDiff(request);

	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);

	const Core::FileDiff& file = diff.GetValue().Files.front();

	CHECK(file.Change == FileChange::Modified);
	CHECK(file.NewPath == std::filesystem::path("a.txt"));
	REQUIRE(file.Hunks.size() == 1);

	const Core::Hunk& hunk = file.Hunks.front();

	CHECK(hunk.OldStart == 1);
	CHECK(hunk.NewStart == 1);

	int addedLineCount = 0;
	int removedLineCount = 0;

	for (const Core::DiffLine& line : hunk.Lines)
	{
		if (line.Kind == Core::DiffLineKind::Added)
		{
			++addedLineCount;
			CHECK(line.Text == "TWO\n");
			CHECK(line.NewLine == 2);
		}
		else if (line.Kind == Core::DiffLineKind::Removed)
		{
			++removedLineCount;
			CHECK(line.Text == "two\n");
			CHECK(line.OldLine == 2);
		}
	}

	CHECK(addedLineCount == 1);
	CHECK(removedLineCount == 1);
}

TEST_CASE("The first commit diffs against nothing and reports every file as added")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::CommitToParent;
	request.From = HeadCommitId(repository);

	const Core::Result<Core::Diff> diff = repository.QueryDiff(request);

	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);
	CHECK(diff.GetValue().Files.front().Change == FileChange::Added);
}

TEST_CASE("An unsaved edit shows up as a working tree change against HEAD")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");
	fixture.Write("a.txt", "one\ntwo\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::WorkTreeToHead;

	const Core::Result<Core::Diff> diff = repository.QueryDiff(request);

	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);
	CHECK(diff.GetValue().Files.front().Change == FileChange::Modified);
}

TEST_CASE("A path restricts the diff to one file")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Write("b.txt", "one\n");
	fixture.Commit("add both");
	fixture.Write("a.txt", "changed\n");
	fixture.Write("b.txt", "changed\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::WorkTreeToHead;
	request.File = std::filesystem::path("b.txt");

	const Core::Result<Core::Diff> diff = repository.QueryDiff(request);

	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);
	CHECK(diff.GetValue().Files.front().NewPath == std::filesystem::path("b.txt"));
}

TEST_CASE("A rename is reported as one renamed file rather than an add and a delete")
{
	GitFixture fixture;

	fixture.Write("old.txt", "one\ntwo\nthree\nfour\n");
	fixture.Commit("add old");
	fixture.Rename("old.txt", "new.txt");
	fixture.Commit("rename");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::CommitToParent;
	request.From = HeadCommitId(repository);

	const Core::Result<Core::Diff> diff = repository.QueryDiff(request);

	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);

	const Core::FileDiff& file = diff.GetValue().Files.front();

	CHECK(file.Change == FileChange::Renamed);
	CHECK(file.OldPath == std::filesystem::path("old.txt"));
	CHECK(file.NewPath == std::filesystem::path("new.txt"));
}

TEST_CASE("A binary file is flagged and carries no hunks")
{
	GitFixture fixture;

	const std::size_t kBinaryByteCount = 5;
	fixture.Write("data.bin", std::string("\0\1\2\0\3", kBinaryByteCount));
	fixture.Commit("add binary");
	fixture.Write("data.bin", std::string("\x00\x09\x09\x00\x03", kBinaryByteCount));
	fixture.Commit("change binary");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::CommitToParent;
	request.From = HeadCommitId(repository);

	const Core::Result<Core::Diff> diff = repository.QueryDiff(request);

	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);
	CHECK(diff.GetValue().Files.front().IsBinary);
	CHECK(diff.GetValue().Files.front().Hunks.empty());
}

TEST_CASE("A file with no trailing newline keeps its last line")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");
	fixture.Write("a.txt", "one\ntwo");
	fixture.Commit("append without a newline");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::CommitToParent;
	request.From = HeadCommitId(repository);

	const Core::Result<Core::Diff> diff = repository.QueryDiff(request);

	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);

	bool sawAddedLine = false;

	for (const Core::Hunk& hunk : diff.GetValue().Files.front().Hunks)
	{
		for (const Core::DiffLine& line : hunk.Lines)
		{
			if (line.Kind == Core::DiffLineKind::Added && line.Text == "two")
			{
				sawAddedLine = true;
			}
		}
	}

	// libgit2 also emits a GIT_DIFF_LINE_DEL_EOFNL / ADD_EOFNL marker line here, which carries
	// no text of its own and must not reach the view as a line of the file.
	CHECK(sawAddedLine);
}

TEST_CASE("An untracked file is an added file with its whole content, in both working tree diffs")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");
	fixture.Write("b.txt", "first\nsecond\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	DiffTarget target = DiffTarget::WorkTreeToIndex;

	SUBCASE("against the index")
	{
		target = DiffTarget::WorkTreeToIndex;
	}
	SUBCASE("against HEAD")
	{
		target = DiffTarget::WorkTreeToHead;
	}

	DiffRequest request;
	request.Target = target;
	request.File = std::filesystem::path("b.txt");

	const Core::Result<Core::Diff> diff = repository.QueryDiff(request);

	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);

	const Core::FileDiff& file = diff.GetValue().Files.front();

	CHECK(file.Change == FileChange::Added);
	CHECK(file.OldPath.empty());
	CHECK(file.NewPath == std::filesystem::path("b.txt"));
	REQUIRE(file.Hunks.size() == 1);
	REQUIRE(file.Hunks.front().Lines.size() == 2);
	CHECK(file.Hunks.front().Lines.front().Text == "first\n");
}

TEST_CASE("A revision diff naming a commit that does not exist is an error, not every file added")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Oid missing = IdOfNoStoredObject();

	DiffRequest request;
	request.Target = DiffTarget::RevisionToRevision;

	SUBCASE("on the older side")
	{
		request.From = missing;
		request.To = HeadCommitId(repository);
	}

	SUBCASE("on the newer side")
	{
		request.From = HeadCommitId(repository);
		request.To = missing;
	}

	const Core::Result<Core::Diff> diff = repository.QueryDiff(request);

	REQUIRE_FALSE(diff.HasValue());
	CHECK(diff.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("A commit-to-parent diff of a commit that does not exist is an error")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Oid missing = IdOfNoStoredObject();

	DiffRequest request;
	request.Target = DiffTarget::CommitToParent;
	request.From = missing;

	CHECK_FALSE(repository.QueryDiff(request).HasValue());
}

TEST_CASE("With no commit yet, the index is all added in both HEAD diffs")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");

	{
		git_repository* stagingRepository = nullptr;
		REQUIRE(git_repository_open(&stagingRepository, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);

		git_index* index = nullptr;
		REQUIRE(git_repository_index(&index, stagingRepository) == 0);
		REQUIRE(git_index_add_bypath(index, "a.txt") == 0);
		REQUIRE(git_index_write(index) == 0);

		git_index_free(index);
		git_repository_free(stagingRepository);
	}

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	DiffRequest request;

	SUBCASE("index against HEAD")
	{
		request.Target = DiffTarget::IndexToHead;
	}
	SUBCASE("working tree against HEAD")
	{
		request.Target = DiffTarget::WorkTreeToHead;
	}

	const Core::Result<Core::Diff> diff = repository.QueryDiff(request);

	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);
	CHECK(diff.GetValue().Files.front().Change == FileChange::Added);
}

TEST_CASE("A revision diff with no older side reports every file of the newer one as added")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::RevisionToRevision;
	request.To = HeadCommitId(repository);

	const Core::Result<Core::Diff> diff = repository.QueryDiff(request);

	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);
	CHECK(diff.GetValue().Files.front().Change == FileChange::Added);
}
