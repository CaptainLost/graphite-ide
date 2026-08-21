#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

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
