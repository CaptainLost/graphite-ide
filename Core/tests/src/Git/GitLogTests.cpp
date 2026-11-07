#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

using Core::CommitSummary;
using Core::LogPage;
using Core::LogRequest;
using Core::Repository;
using Core::Result;

namespace {

	// Compared as text so that a failing check prints hashes a reader can look up rather than
	// twenty raw bytes.
	std::unordered_set<std::string> UnpushedHexes(const LogPage& page)
	{
		std::unordered_set<std::string> hexes;

		for (const Core::Oid& id : page.Unpushed)
		{
			hexes.insert(id.ToHex());
		}

		return hexes;
	}
}

TEST_CASE("The log lists commits newest first")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	const std::string second = fixture.Commit("add b");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});

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

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);

	const CommitSummary& commit = log.GetValue().Commits.front();

	CHECK(commit.Author.Name == "Test Author");
	CHECK(commit.Author.Email == "test@example.com");
	CHECK(commit.Summary == "add a");
	CHECK(commit.Body == "A longer explanation.\n");
	CHECK(commit.Parents.empty());
}

TEST_CASE("MaxCount stops the walk early and marks the page incomplete")
{
	GitFixture fixture;

	const int commitCount = 5;

	for (int commitNumber = 0; commitNumber < commitCount; ++commitNumber)
	{
		fixture.Write("a.txt", std::to_string(commitNumber) + "\n");
		fixture.Commit("edit " + std::to_string(commitNumber));
	}

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.MaxCount = 2;

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.size() == 2);
	CHECK_FALSE(log.GetValue().Complete);
}

TEST_CASE("A log that runs out of history on its own well short of MaxCount and MaxWalk reports the page as complete")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
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

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});

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

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.Ref = "refs/heads/side";

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 2);
	CHECK(log.GetValue().Commits[0].Summary == "add side");
}

TEST_CASE("Log: a history asked for an annotated tag's full name starts at its commit")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "two\n");
	const std::string tagged = fixture.Commit("second");
	fixture.Write("a.txt", "three\n");
	fixture.Commit("third");
	fixture.TagAnnotated("v1", tagged, 1500000000, "Release\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.Ref = "refs/tags/v1";

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 2);
	CHECK(log.GetValue().Commits[0].Summary == "second");
	CHECK(log.GetValue().Commits[1].Summary == "first");
}

TEST_CASE("The log of a repository with no commits is empty rather than an error because an unborn HEAD is a legitimate state")
{
	GitFixture fixture;

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.empty());
}

TEST_CASE("A short id keeps the leading digits of the full one")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string full = fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);

	const std::size_t firstDigitOffset = 0;
	const std::size_t defaultShortDigits = 7;
	const std::size_t longerShortDigits = 12;

	CHECK(log.GetValue().Commits.front().Id.ShortHex() == full.substr(firstDigitOffset, defaultShortDigits));
	CHECK(log.GetValue().Commits.front().Id.ShortHex(longerShortDigits) == full.substr(firstDigitOffset, longerShortDigits));
}

TEST_CASE("A CRLF message keeps its body without a leading blank line")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a\r\n\r\nA longer explanation.\r\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);

	CHECK(log.GetValue().Commits.front().Body == "A longer explanation.\r\n");
}

TEST_CASE("A second blank line between title and body stays in the body")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a\n\n\nA longer explanation.\n");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 1);

	CHECK(log.GetValue().Commits.front().Body == "\nA longer explanation.\n");
}

TEST_CASE("Querying a ref that does not exist is an error")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.Ref = "refs/heads/nope";

	const Result<LogPage> log = repository.QueryLog(request);

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

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});

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

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().UpstreamKnown);
	CHECK(log.GetValue().Unpushed.empty());
}

TEST_CASE("A branch with no upstream reports nothing rather than everything and says its upstream is unknown")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	CHECK_FALSE(log.GetValue().UpstreamKnown);
	CHECK(log.GetValue().Unpushed.empty());
}

TEST_CASE("The log of a remote-tracking ref has no upstream of its own so nothing is unpushed")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string shared = fixture.Commit("add a");

	fixture.PushTo("master", shared);

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.Ref = "refs/remotes/origin/master";

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK_FALSE(log.GetValue().UpstreamKnown);
	CHECK(log.GetValue().Unpushed.empty());
}

TEST_CASE("A merged branch's own commits count as unpushed alongside the merge as neither is reachable from the upstream")
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

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().UpstreamKnown);
	CHECK(UnpushedHexes(log.GetValue()) == std::unordered_set<std::string>{merge, side});
}

TEST_CASE("A page says how many commits its walk visited, which without a filter is the commits it kept")
{
	GitFixture fixture;

	const int commitCount = 5;

	for (int commitNumber = 0; commitNumber < commitCount; ++commitNumber)
	{
		fixture.Write("a.txt", std::to_string(commitNumber) + "\n");
		fixture.Commit("edit " + std::to_string(commitNumber));
	}

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.MaxCount = 2;

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Walked == 2);
}

TEST_CASE("A complete page has visited every commit of its history")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b");

	fixture.Write("c.txt", "three\n");
	fixture.Commit("add c");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Complete);
	CHECK(log.GetValue().Walked == 3);
}

TEST_CASE("A history of exactly MaxCount commits is complete, since nothing is left past the page")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b");

	fixture.Write("c.txt", "three\n");
	fixture.Commit("add c");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.MaxCount = 3;

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.size() == 3);
	CHECK(log.GetValue().Complete);
	CHECK(log.GetValue().Walked == 3);
}

TEST_CASE("The default page and walk ceiling are the first page the history shows")
{
	CHECK(LogRequest{}.MaxCount == Core::kLogPageSize);
	CHECK(LogRequest{}.MaxWalk == Core::kLogWalkLimit);
	CHECK(Core::kLogPageSize == 500);
	CHECK(Core::kLogWalkLimit == 20000);
}

TEST_CASE("Counting commits counts every commit reachable from the tip, itself included")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b");

	fixture.Write("c.txt", "three\n");
	fixture.Commit("add c");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<Core::Oid> head = repository.HeadCommit();
	REQUIRE(head.HasValue());

	const Result<std::size_t> count = repository.CountCommits(head.GetValue());

	REQUIRE(count.HasValue());
	CHECK(count.GetValue() == 3);
}

TEST_CASE("Counting from an older commit counts only the history behind it")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	fixture.Commit("add b");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> log = repository.QueryLog(LogRequest{});
	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 2);

	const Result<std::size_t> count = repository.CountCommits(log.GetValue().Commits.back().Id);

	REQUIRE(count.HasValue());
	CHECK(count.GetValue() == 1);
}

TEST_CASE("Counting through a merge counts each side's commits once")
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
	fixture.MergeCommit("merge side", "side");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<Core::Oid> head = repository.HeadCommit();
	REQUIRE(head.HasValue());

	const Result<std::size_t> count = repository.CountCommits(head.GetValue());

	REQUIRE(count.HasValue());
	CHECK(count.GetValue() == 4);
}

TEST_CASE("Git: counting first parents only counts a merge and not the commits it merged")
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
	fixture.MergeCommit("merge side", "side");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<Core::Oid> head = repository.HeadCommit();
	REQUIRE(head.HasValue());

	const Result<std::size_t> firstParents = repository.CountCommits(head.GetValue(), Core::FollowedParents::FirstOnly);
	const Result<std::size_t> every = repository.CountCommits(head.GetValue(), Core::FollowedParents::All);

	REQUIRE(firstParents.HasValue());
	REQUIRE(every.HasValue());
	CHECK(firstParents.GetValue() == 3);
	CHECK(every.GetValue() == 4);
}

TEST_CASE("Counting from a commit the repository does not hold is NotFound")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	constexpr std::uint8_t kAbsentByte = 0xAB;
	Core::Oid absent;
	absent.Bytes.fill(kAbsentByte);

	const Result<std::size_t> count = repository.CountCommits(absent);

	REQUIRE_FALSE(count.HasValue());
	CHECK(count.GetError().Code == Core::ErrorCode::NotFound);
}

namespace {

	std::vector<std::string> SummariesOf(const LogPage& page)
	{
		std::vector<std::string> summaries;

		for (const CommitSummary& commit : page.Commits)
		{
			summaries.push_back(commit.Summary);
		}

		return summaries;
	}
}

TEST_CASE("Git: a first-parent log lists a merge and not the commits it merged")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("base");
	fixture.Branch("side");
	fixture.Checkout("side");
	fixture.Write("side.txt", "side\n");
	fixture.Commit("side work");
	fixture.Checkout("master");
	fixture.Write("main.txt", "main\n");
	fixture.Commit("main work");
	fixture.MergeCommit("merge side", "side");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.FirstParentOnly = true;

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(SummariesOf(log.GetValue()) == std::vector<std::string>{"merge side", "main work", "base"});
}

TEST_CASE("Git: a log from a merge's second parent without its first parent lists what the merge brought")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("base");
	fixture.Branch("side");
	fixture.Checkout("side");
	fixture.Write("side.txt", "side\n");
	fixture.Commit("side one");
	fixture.Write("side.txt", "side two\n");
	fixture.Commit("side two");
	fixture.Checkout("master");
	fixture.Write("main.txt", "main\n");
	fixture.Commit("main work");
	fixture.MergeCommit("merge side", "side");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Result<LogPage> all = repository.QueryLog(LogRequest{});
	REQUIRE(all.HasValue());
	REQUIRE_FALSE(all.GetValue().Commits.empty());

	const CommitSummary& merge = all.GetValue().Commits.front();
	REQUIRE(merge.Parents.size() == 2);

	LogRequest request;
	request.From = merge.Parents[1];
	request.Exclude = merge.Parents[0];
	request.FirstParentOnly = true;

	const Result<LogPage> brought = repository.QueryLog(request);

	REQUIRE(brought.HasValue());
	CHECK(SummariesOf(brought.GetValue()) == std::vector<std::string>{"side two", "side one"});
	CHECK_FALSE(brought.GetValue().UpstreamKnown);
	CHECK(brought.GetValue().Unpushed.empty());
}

TEST_CASE("Git: a log from two tips walks both with children before parents")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("base");
	fixture.Branch("side");
	fixture.Checkout("side");
	fixture.Write("side.txt", "side\n");
	const std::string sideTip = fixture.Commit("side work");
	fixture.Checkout("master");
	fixture.Write("main.txt", "main\n");
	fixture.Commit("main work");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	const std::optional<Core::Oid> sideTipId = Core::Oid::FromHex(sideTip);
	REQUIRE(sideTipId.has_value());

	LogRequest request;
	request.AlsoFrom.push_back(*sideTipId);

	const Result<LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	REQUIRE(log.GetValue().Commits.size() == 3);

	std::unordered_set<std::string> seen;

	for (const CommitSummary& commit : log.GetValue().Commits)
	{
		for (const Core::Oid& parent : commit.Parents)
		{
			CHECK_FALSE(seen.contains(parent.ToHex()));
		}

		seen.insert(commit.Id.ToHex());
	}

	CHECK(log.GetValue().Commits.back().Summary == "base");
	CHECK(seen.contains(sideTip));
}
