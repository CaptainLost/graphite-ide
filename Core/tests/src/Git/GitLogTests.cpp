#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <string>
#include <unordered_set>
#include <vector>

using Core::CommitSummary;
using Core::LogRequest;
using Core::Repository;

namespace {

	// The unpushed set as text, so a failing test prints hashes a reader can look up rather
	// than twenty raw bytes.
	std::unordered_set<std::string> UnpushedHexes(const Core::LogPage& page)
	{
		std::unordered_set<std::string> hexes;

		for (const Core::Oid& id : page.Unpushed)
		{
			hexes.insert(id.ToHex());
		}

		return hexes;
	}

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
	REQUIRE(log.GetValue().Commits.size() == 2);
	CHECK(log.GetValue().Commits[0].Id.ToHex() == second);
	CHECK(log.GetValue().Commits[1].Id.ToHex() == first);
}

TEST_CASE("A commit carries its author, summary and body")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a\n\nA longer explanation.\n");

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);

	const CommitSummary& commit = log.GetValue().Commits.front();

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
	CHECK(log.GetValue().Commits.size() == 2);

	// Five commits exist and only two were kept: the page cannot be the whole history,
	// whether or not a caller happens to compare the count against MaxCount itself.
	CHECK_FALSE(log.GetValue().Complete);
}

TEST_CASE("A log that runs out of history on its own reports the page as complete")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());

	// The walk reached the end of history on its own, well short of MaxCount and MaxWalk:
	// nothing was stopped early, so the page is the whole answer.
	CHECK(log.GetValue().Complete);
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
	REQUIRE_FALSE(log.GetValue().Commits.empty());

	const CommitSummary& merge = log.GetValue().Commits.front();

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
	REQUIRE(log.GetValue().Commits.size() == 2);
	CHECK(log.GetValue().Commits[0].Summary == "add side");
}

TEST_CASE("The log of a repository with no commits is empty rather than an error")
{
	GitFixture fixture;

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	// An unborn HEAD is a state a fresh repository is legitimately in. Reporting it as
	// an error would make the panel show a failure for a repository that is merely new.
	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.empty());
}

TEST_CASE("A short id keeps the leading digits of the full one")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string full = fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);

	CHECK(log.GetValue().Commits.front().Id.ShortHex() == full.substr(0, 7));
	CHECK(log.GetValue().Commits.front().Id.ShortHex(12) == full.substr(0, 12));
}

TEST_CASE("A CRLF message keeps its body without a leading blank line")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a\r\n\r\nA longer explanation.\r\n");

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);

	CHECK(log.GetValue().Commits.front().Body == "A longer explanation.\r\n");
}

TEST_CASE("A second blank line between title and body stays in the body")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a\n\n\nA longer explanation.\n");

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);

	// Only the single separator line is consumed; a second blank line is the body's own.
	CHECK(log.GetValue().Commits.front().Body == "\nA longer explanation.\n");
}

TEST_CASE("Querying a ref that does not exist is an error")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.Ref = "refs/heads/nope";

	auto log = repository.QueryLog(request);

	REQUIRE_FALSE(log.HasValue());
	CHECK(log.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("Commits the upstream does not have are reported as unpushed")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string shared = fixture.Commit("add a");

	fixture.PushTo("master", shared);

	fixture.Write("b.txt", "two\n");
	const std::string local = fixture.Commit("add b");

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().UpstreamKnown);
	CHECK(UnpushedHexes(log.GetValue()) == std::unordered_set<std::string>{local});
}

TEST_CASE("A branch level with its upstream has nothing unpushed")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string only = fixture.Commit("add a");

	fixture.PushTo("master", only);

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().UpstreamKnown);
	CHECK(log.GetValue().Unpushed.empty());
}

TEST_CASE("A branch with no upstream reports nothing rather than everything")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());

	// Not "every commit is unpushed": without an upstream the question has no answer, and
	// UpstreamKnown is what says so. A view that marked the whole history here would be
	// stating a distance nobody measured.
	CHECK_FALSE(log.GetValue().UpstreamKnown);
	CHECK(log.GetValue().Unpushed.empty());
}

TEST_CASE("The log of a remote-tracking ref has nothing unpushed")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string shared = fixture.Commit("add a");

	fixture.PushTo("master", shared);

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.Ref = "refs/remotes/origin/master";

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());

	// A remote-tracking branch has no upstream of its own, so nothing in its history is
	// waiting to be pushed anywhere.
	CHECK_FALSE(log.GetValue().UpstreamKnown);
	CHECK(log.GetValue().Unpushed.empty());
}

TEST_CASE("A merged branch's own commits count as unpushed")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string shared = fixture.Commit("add a");

	fixture.PushTo("master", shared);

	fixture.Branch("side");
	fixture.Checkout("side");
	fixture.Write("b.txt", "two\n");
	const std::string side = fixture.Commit("add b");

	fixture.Checkout("master");
	const std::string merge = fixture.MergeCommit("merge side", "side");

	const Repository repository = OpenAt(fixture.GetPath());

	auto log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());

	// Both, and for the same reason: neither is reachable from origin/master. That is also
	// what the branch's own ahead count says, so the list and the count agree.
	CHECK(log.GetValue().UpstreamKnown);
	CHECK(UnpushedHexes(log.GetValue()) == std::unordered_set<std::string>{merge, side});
}
