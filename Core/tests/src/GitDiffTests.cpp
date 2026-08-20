#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <string>

using Core::DiffRequest;
using Core::DiffTarget;
using Core::FileChange;
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
		REQUIRE_FALSE(log.GetValue().empty());
		return log.GetValue().front().Id;
	}
}

TEST_CASE("A commit diffs against its parent")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\ntwo\nthree\n");
	fixture.Commit("add a");
	fixture.Write("a.txt", "one\nTWO\nthree\n");
	fixture.Commit("shout");

	const Repository repository = OpenAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::CommitToParent;
	request.From = HeadId(repository);

	auto diff = repository.QueryDiff(request);

	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);

	const Core::FileDiff& file = diff.GetValue().Files.front();

	CHECK(file.Change == FileChange::Modified);
	CHECK(file.NewPath == std::filesystem::path("a.txt"));
	REQUIRE(file.Hunks.size() == 1);

	const Core::Hunk& hunk = file.Hunks.front();

	CHECK(hunk.OldStart == 1);
	CHECK(hunk.NewStart == 1);

	int added = 0;
	int removed = 0;

	for (const Core::DiffLine& line : hunk.Lines)
	{
		if (line.Kind == Core::DiffLineKind::Added)
		{
			++added;
			CHECK(line.Text == "TWO\n");
			CHECK(line.NewLine == 2);
		}
		else if (line.Kind == Core::DiffLineKind::Removed)
		{
			++removed;
			CHECK(line.Text == "two\n");
			CHECK(line.OldLine == 2);
		}
	}

	CHECK(added == 1);
	CHECK(removed == 1);
}

TEST_CASE("The first commit diffs against nothing and reports every file as added")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::CommitToParent;
	request.From = HeadId(repository);

	auto diff = repository.QueryDiff(request);

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

	const Repository repository = OpenAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::WorkTreeToHead;

	auto diff = repository.QueryDiff(request);

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

	const Repository repository = OpenAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::WorkTreeToHead;
	request.File = std::filesystem::path("b.txt");

	auto diff = repository.QueryDiff(request);

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

	const Repository repository = OpenAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::CommitToParent;
	request.From = HeadId(repository);

	auto diff = repository.QueryDiff(request);

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

	fixture.Write("data.bin", std::string("\0\1\2\0\3", 5));
	fixture.Commit("add binary");
	fixture.Write("data.bin", std::string("\x00\x09\x09\x00\x03", 5));
	fixture.Commit("change binary");

	const Repository repository = OpenAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::CommitToParent;
	request.From = HeadId(repository);

	auto diff = repository.QueryDiff(request);

	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);

	// The view must not try to render bytes as lines, and the only warning it gets is
	// this flag: libgit2 emits no hunks for a binary delta.
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

	const Repository repository = OpenAt(fixture.GetPath());

	DiffRequest request;
	request.Target = DiffTarget::CommitToParent;
	request.From = HeadId(repository);

	auto diff = repository.QueryDiff(request);

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

	// libgit2 also emits a GIT_DIFF_LINE_DEL_EOFNL / ADD_EOFNL marker line here. It
	// carries no text of its own and must not reach the view as a line of the file.
	CHECK(sawAddedLine);
}
