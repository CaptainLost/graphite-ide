#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <string>

using Core::LogPage;
using Core::LogRequest;
using Core::Repository;
using Core::Result;

TEST_CASE("A filtered log keeps only the commits the filter matches")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	const std::string wanted = fixture.Commit("teach the reader about encoding");

	fixture.Write("c.txt", "three\n");
	fixture.Commit("add c");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.Filter.Text = "encoding";

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);
	CHECK(log.GetValue().Commits.front().Id.ToHex() == wanted);
}

TEST_CASE("A filtered log that reached the end of history is complete however short its page")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.Filter.Text = "add b";

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.size() == 1);
	CHECK(log.GetValue().Complete);
}

TEST_CASE("A filter that matches nothing answers an empty page")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.Filter.Text = "nothing here says this";

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.empty());
	CHECK(log.GetValue().Complete);
}

TEST_CASE("A filtered walk counts matching and non-matching commits against the walk ceiling and says it stopped")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("the oldest commit");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b");

	fixture.Write("c.txt", "three\n");
	fixture.Commit("add c");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.Filter.Text = "oldest";
	request.MaxWalk = 1;

	const Result<LogPage> log = repository.QueryLog(request);

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

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.Filter.Text = "keep me";
	request.MaxCount = 1;

	const Result<LogPage> log = repository.QueryLog(request);

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

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.File = "Application.cpp";

	const Result<LogPage> log = repository.QueryLog(request);

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

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.File = "*.cpp";

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.size() == 2);
}

TEST_CASE("A full path as the file and line histories pass it narrows to exactly that file and not to a file sharing its tail")
{
	GitFixture fixture;

	fixture.Write("src/App/Application.cpp", "one\n");
	const std::string wanted = fixture.Commit("write the application");

	fixture.Write("tests/App/Application.cpp", "two\n");
	fixture.Commit("write its test");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.File = "src/App/Application.cpp";

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);
	CHECK(log.GetValue().Commits.front().Id.ToHex() == wanted);
}

TEST_CASE("A bare file name follows the file through a rename back to the commit that created it under its old name")
{
	GitFixture fixture;

	fixture.Write("old.txt", "one\n");
	fixture.Commit("add old");

	fixture.Rename("old.txt", "new.txt");
	fixture.Commit("rename to new");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.File = "new.txt";
	request.FollowRenames = true;

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.size() == 2);
}

TEST_CASE("A wildcard pattern is not narrowed onto one renamed file so it keeps the commits to every file it names")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b");

	fixture.Rename("a.txt", "renamed.txt");
	fixture.Commit("rename a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.File = "*.txt";
	request.FollowRenames = true;

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.size() == 3);
}

TEST_CASE("A filtered page counts the commits it passed over in Walked and not only the ones it kept")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("keep the oldest");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("skip me");

	fixture.Write("c.txt", "three\n");
	fixture.Commit("skip me too");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.Filter.Text = "keep";

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.size() == 1);
	CHECK(log.GetValue().Complete);
	CHECK(log.GetValue().Walked == 3);
}

TEST_CASE("A filtered page stopped at the walk ceiling says it visited exactly that many")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("the oldest commit");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b");

	fixture.Write("c.txt", "three\n");
	fixture.Commit("add c");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.Filter.Text = "oldest";
	request.MaxWalk = 2;

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.empty());
	CHECK_FALSE(log.GetValue().Complete);
	CHECK(log.GetValue().Walked == 2);
}
