#include <doctest/doctest.h>

#include "Core/Git/Blame.h"
#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <string>

using Core::Blame;
using Core::BlameHunk;
using Core::BlameRequest;
using Core::HunkOfLine;
using Core::Oid;
using Core::Repository;
using Core::Result;

namespace {

	Oid OidOf(const std::string& hex)
	{
		return Oid::FromHex(hex).value();
	}

	Blame BlameOf(const Repository& repository, const std::string& file)
	{
		const Result<Blame> blame = repository.QueryBlame(BlameRequest{file, HeadCommitId(repository)});
		REQUIRE(blame.HasValue());
		return blame.GetValue();
	}
}

TEST_CASE("Blame attributes each line to the commit that last changed it")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\ntwo\nthree\n");
	const Oid first = OidOf(fixture.CommitAs("add", "Ann", "ann@example.com"));

	fixture.Write("a.txt", "one\nTWO\nthree\n");
	const Oid second = OidOf(fixture.CommitAs("edit two", "Bob", "bob@example.com"));

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Blame blame = BlameOf(repository, "a.txt");

	CHECK(blame.LineCount == 3);
	REQUIRE(HunkOfLine(blame, 1) != nullptr);
	CHECK(HunkOfLine(blame, 1)->Commit == first);
	CHECK(HunkOfLine(blame, 2)->Commit == second);
	CHECK(HunkOfLine(blame, 3)->Commit == first);

	CHECK(blame.Commits.at(second).Summary == "edit two");
	CHECK(blame.Commits.at(second).Author.Name == "Bob");
	CHECK(blame.Commits.at(first).Author.Name == "Ann");
	CHECK(blame.Commits.size() == 2);
}

TEST_CASE("Blame gives each line its number in the commit that last changed it")
{
	GitFixture fixture;

	fixture.Write("a.txt", "a\nb\n");
	fixture.Commit("add");

	fixture.Write("a.txt", "new\na\nb\n");
	fixture.Commit("prepend");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Blame blame = BlameOf(repository, "a.txt");

	const BlameHunk* hunk = HunkOfLine(blame, 2);
	REQUIRE(hunk != nullptr);
	CHECK(hunk->OriginalFirstLine + (2 - hunk->FirstLine) == 1);
}

TEST_CASE("Blame follows a file through a rename")
{
	GitFixture fixture;

	fixture.Write("old.txt", "1\n2\n3\n4\n");
	const Oid first = OidOf(fixture.Commit("add"));

	fixture.Rename("old.txt", "new.txt");
	fixture.Commit("rename");

	fixture.Write("new.txt", "1\n2\n3\nfour\n");
	fixture.Commit("edit four");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Blame blame = BlameOf(repository, "new.txt");

	const BlameHunk* hunk = HunkOfLine(blame, 1);
	REQUIRE(hunk != nullptr);
	CHECK(hunk->OriginalPath == std::filesystem::path{"old.txt"});
	CHECK(hunk->Commit == first);
}

TEST_CASE("Blame of a path not in the commit is NotFound")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Result<Blame> blame = repository.QueryBlame(BlameRequest{"missing.txt", HeadCommitId(repository)});

	REQUIRE_FALSE(blame.HasValue());
	CHECK(blame.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("Blame counts a file without a final newline")
{
	GitFixture fixture;

	fixture.Write("a.txt", "a\nb");
	fixture.Commit("add");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Blame blame = BlameOf(repository, "a.txt");

	CHECK(blame.LineCount == 2);
}

TEST_CASE("Blame finds no hunk for line zero or a line past the end")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\ntwo\n");
	fixture.Commit("add");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Blame blame = BlameOf(repository, "a.txt");

	CHECK(HunkOfLine(blame, 0) == nullptr);
	CHECK(HunkOfLine(blame, 1) != nullptr);
	CHECK(HunkOfLine(blame, 2) != nullptr);
	CHECK(HunkOfLine(blame, 3) == nullptr);
}
