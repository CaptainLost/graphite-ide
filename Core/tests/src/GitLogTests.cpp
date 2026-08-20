#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <string>
#include <vector>

using Core::CommitSummary;
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

TEST_CASE("The log lists commits newest first")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	const std::string second = fixture.Commit("add b");

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().size() == 2);
	CHECK(log.GetValue()[0].Id.ToHex() == second);
	CHECK(log.GetValue()[1].Id.ToHex() == first);
}

TEST_CASE("A commit carries its author, summary and body")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a\n\nA longer explanation.\n");

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().size() == 1);

	const CommitSummary& commit = log.GetValue().front();

	CHECK(commit.Author.Name == "Test Author");
	CHECK(commit.Author.Email == "test@example.com");
	CHECK(commit.Summary == "add a");
	CHECK(commit.Body == "A longer explanation.\n");
	CHECK(commit.Parents.empty());
}

TEST_CASE("MaxCount stops the walk early")
{
	GitFixture fixture;

	for (int index = 0; index < 5; ++index)
	{
		fixture.Write("a.txt", std::to_string(index) + "\n");
		fixture.Commit("edit " + std::to_string(index));
	}

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.MaxCount = 2;

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().size() == 2);
}

TEST_CASE("A merge commit reports both parents in order")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");
	fixture.Branch("side");
	fixture.Checkout("side");
	fixture.Write("side.txt", "side\n");
	const std::string sideTip = fixture.Commit("add side");
	fixture.Checkout("master");
	fixture.Write("main.txt", "main\n");
	const std::string mainTip = fixture.Commit("add main");
	fixture.MergeCommit("merge side", "side");

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	REQUIRE_FALSE(log.GetValue().empty());

	const CommitSummary& merge = log.GetValue().front();

	REQUIRE(merge.Parents.size() == 2);
	CHECK(merge.Parents[0].ToHex() == mainTip);
	CHECK(merge.Parents[1].ToHex() == sideTip);
}

TEST_CASE("A named ref limits the walk to that branch")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");
	fixture.Branch("side");
	fixture.Checkout("side");
	fixture.Write("side.txt", "side\n");
	fixture.Commit("add side");
	fixture.Checkout("master");
	fixture.Write("main.txt", "main\n");
	fixture.Commit("add main");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.Ref = "refs/heads/side";

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().size() == 2);
	CHECK(log.GetValue()[0].Summary == "add side");
}

TEST_CASE("The log of a repository with no commits is empty rather than an error")
{
	GitFixture fixture;

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	// An unborn HEAD is a state a fresh repository is legitimately in. Reporting it as
	// an error would make the panel show a failure for a repository that is merely new.
	REQUIRE(log.HasValue());
	CHECK(log.GetValue().empty());
}

TEST_CASE("A short id keeps the leading digits of the full one")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string full = fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().size() == 1);

	CHECK(log.GetValue().front().Id.ShortHex() == full.substr(0, 7));
	CHECK(log.GetValue().front().Id.ShortHex(12) == full.substr(0, 12));
}
