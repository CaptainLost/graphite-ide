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
}

TEST_CASE("A filtered log keeps only the commits the filter matches")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	const std::string wanted = fixture.Commit("teach the reader about encoding");

	fixture.Write("c.txt", "three\n");
	fixture.Commit("add c");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.Filter.Text = "encoding";

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);
	CHECK(log.GetValue().Commits.front().Id.ToHex() == wanted);
}

TEST_CASE("A filtered log that reached the end of history is complete")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.Filter.Text = "add b";

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.size() == 1);

	// A short page is the normal outcome of a filter, and saying it is incomplete would
	// leave the reader with a "load more" that can only ever find the same nothing.
	CHECK(log.GetValue().Complete);
}

TEST_CASE("A filter that matches nothing answers an empty page")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.Filter.Text = "nothing here says this";

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.empty());
	CHECK(log.GetValue().Complete);
}

TEST_CASE("A filtered walk stops at the walk ceiling and says so")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("the oldest commit");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b");

	fixture.Write("c.txt", "three\n");
	fixture.Commit("add c");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.Filter.Text = "oldest";

	// Every commit the walk passes is measured against the filter, matching or not, so a
	// narrow filter over a long history is bounded by this and nothing else.
	request.MaxWalk = 1;

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.empty());
	CHECK_FALSE(log.GetValue().Complete);
}

TEST_CASE("The page size counts the commits kept, not the commits passed")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("keep me");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("skip me");

	fixture.Write("c.txt", "three\n");
	const std::string newest = fixture.Commit("keep me too");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.Filter.Text = "keep me";
	request.MaxCount = 1;

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);
	CHECK(log.GetValue().Commits.front().Id.ToHex() == newest);
	CHECK_FALSE(log.GetValue().Complete);
}

TEST_CASE("A bare file name narrows the history to that file, wherever it sits")
{
	GitFixture fixture;

	fixture.Write("src/App/Application.cpp", "one\n");
	const std::string wanted = fixture.Commit("write the application");

	fixture.Write("src/App/Other.cpp", "two\n");
	fixture.Commit("write something else");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = "Application.cpp";

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);
	CHECK(log.GetValue().Commits.front().Id.ToHex() == wanted);
}

TEST_CASE("A pattern narrows the history to every file it names")
{
	GitFixture fixture;

	fixture.Write("src/App/Application.cpp", "one\n");
	fixture.Commit("write the application");

	fixture.Write("docs/notes.md", "two\n");
	fixture.Commit("write the notes");

	fixture.Write("src/App/Other.cpp", "three\n");
	fixture.Commit("write something else");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = "*.cpp";

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.size() == 2);
}

TEST_CASE("A full path still narrows to exactly that file")
{
	// The form the file and line histories pass. A pattern that also matched a tail would
	// make those two silently wider than the file they were opened on.
	GitFixture fixture;

	fixture.Write("src/App/Application.cpp", "one\n");
	const std::string wanted = fixture.Commit("write the application");

	fixture.Write("tests/App/Application.cpp", "two\n");
	fixture.Commit("write its test");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = "src/App/Application.cpp";

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);
	CHECK(log.GetValue().Commits.front().Id.ToHex() == wanted);
}

TEST_CASE("A bare file name follows the file through a rename")
{
	GitFixture fixture;

	fixture.Write("old.txt", "one\n");
	fixture.Commit("add old");

	fixture.Rename("old.txt", "new.txt");
	fixture.Commit("rename to new");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = "new.txt";
	request.FollowRenames = true;

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());

	// The commit that created the file is part of its history even though it never used
	// this name — which is the whole point of following renames, and must survive the name
	// being given as a pattern rather than as a path.
	CHECK(log.GetValue().Commits.size() == 2);
}

TEST_CASE("A wildcard pattern is not narrowed onto one file by a rename")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b");

	fixture.Rename("a.txt", "renamed.txt");
	fixture.Commit("rename a");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = "*.txt";
	request.FollowRenames = true;

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());

	// A pattern names a set of files, so there is no one identity for a rename to carry:
	// swapping the pattern for the renamed file's old path would drop "add b" from a
	// question that plainly included it.
	CHECK(log.GetValue().Commits.size() == 3);
}
