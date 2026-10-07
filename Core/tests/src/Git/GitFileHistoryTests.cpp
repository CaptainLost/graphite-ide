#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"

#include <cstddef>
#include <string>
#include <vector>

using Core::LogRequest;
using Core::Repository;

namespace {

	std::vector<std::string> Summaries(const std::vector<Core::CommitSummary>& commits)
	{
		std::vector<std::string> summaries;
		summaries.reserve(commits.size());

		for (const Core::CommitSummary& commit : commits)
		{
			summaries.push_back(commit.Summary);
		}

		return summaries;
	}
}

TEST_CASE("A file's history skips commits that did not touch it")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");
	fixture.Write("b.txt", "one\n");
	fixture.Commit("add b");
	fixture.Write("a.txt", "two\n");
	fixture.Commit("edit a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("a.txt");

	const Core::Result<Core::LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(Summaries(log.GetValue().Commits) == std::vector<std::string>{"edit a", "add a"});
}

TEST_CASE("A file's history continues through a rename when following is on")
{
	GitFixture fixture;

	fixture.Write("old.txt", "one\ntwo\nthree\nfour\n");
	fixture.Commit("add old");
	fixture.Rename("old.txt", "new.txt");
	fixture.Commit("rename to new");
	fixture.Write("new.txt", "one\ntwo\nthree\nfive\n");
	fixture.Commit("edit new");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("new.txt");
	request.FollowRenames = true;

	const Core::Result<Core::LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(Summaries(log.GetValue().Commits) == std::vector<std::string>{"edit new", "rename to new", "add old"});
}

TEST_CASE("A file's history stops at the rename when following is off")
{
	GitFixture fixture;

	fixture.Write("old.txt", "one\ntwo\nthree\nfour\n");
	fixture.Commit("add old");
	fixture.Rename("old.txt", "new.txt");
	fixture.Commit("rename to new");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("new.txt");
	request.FollowRenames = false;

	const Core::Result<Core::LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(Summaries(log.GetValue().Commits) == std::vector<std::string>{"rename to new"});
}

TEST_CASE("A file's history counts MaxCount in kept commits, not in walked ones")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("touch a once");

	const int kNoiseCommitsBetweenTheTwoTouches = 10;
	for (int noiseNumber = 0; noiseNumber < kNoiseCommitsBetweenTheTwoTouches; ++noiseNumber)
	{
		fixture.Write("noise.txt", std::to_string(noiseNumber) + "\n");
		fixture.Commit("noise " + std::to_string(noiseNumber));
	}

	fixture.Write("a.txt", "two\n");
	fixture.Commit("touch a again");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("a.txt");
	request.MaxCount = 2;

	const Core::Result<Core::LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(Summaries(log.GetValue().Commits) == std::vector<std::string>{"touch a again", "touch a once"});
}

TEST_CASE("The history of a path that never existed is empty rather than an error")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("missing.txt");

	const Core::Result<Core::LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.empty());
}

TEST_CASE("A line range's history skips commits that edited elsewhere in the file")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\ntwo\nthree\nfour\nfive\n");
	fixture.Commit("add a");

	fixture.Write("a.txt", "one\ntwo\nthree\nfour\nFIVE\n");
	fixture.Commit("edit the bottom");

	fixture.Write("a.txt", "ONE\ntwo\nthree\nfour\nFIVE\n");
	fixture.Commit("edit the top");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("a.txt");
	request.Lines = Core::OneBasedLineRange{.First = 1, .Last = 1};

	const Core::Result<Core::LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(Summaries(log.GetValue().Commits) == std::vector<std::string>{"edit the top", "add a"});
}

TEST_CASE("A line range's history continues past a rename the rename commit did not touch, though a pure rename has no hunks")
{
	GitFixture fixture;

	fixture.Write("old.txt", "one\ntwo\nthree\nfour\n");
	fixture.Commit("add old");

	fixture.Rename("old.txt", "new.txt");
	fixture.Commit("rename to new");

	fixture.Write("new.txt", "ONE\ntwo\nthree\nfour\n");
	fixture.Commit("edit the top");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	const std::size_t kLineUntouchedSinceAddOld = 4;
	request.File = std::filesystem::path("new.txt");
	request.Lines = Core::OneBasedLineRange{.First = kLineUntouchedSinceAddOld, .Last = kLineUntouchedSinceAddOld};
	request.FollowRenames = true;

	const Core::Result<Core::LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(Summaries(log.GetValue().Commits) == std::vector<std::string>{"add old"});
}

TEST_CASE("A file's history walk stops at MaxWalk even though nothing else limits it, and marks the page incomplete")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("touch a");

	const int kNoiseCommitsAboveTheTouch = 20;
	for (int noiseNumber = 0; noiseNumber < kNoiseCommitsAboveTheTouch; ++noiseNumber)
	{
		fixture.Write("noise.txt", std::to_string(noiseNumber) + "\n");
		fixture.Commit("noise " + std::to_string(noiseNumber));
	}

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("a.txt");
	const std::size_t kWalkCeilingBelowTheTouch = 5;
	request.MaxWalk = kWalkCeilingBelowTheTouch;

	const Core::Result<Core::LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(log.GetValue().Commits.empty());
	CHECK_FALSE(log.GetValue().Complete);
}

TEST_CASE("A file's history that runs out on its own reports the page as complete")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("touch a");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("a.txt");

	const Core::Result<Core::LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(Summaries(log.GetValue().Commits) == std::vector<std::string>{"touch a"});
	CHECK(log.GetValue().Complete);
}

TEST_CASE("A line range's history follows the lines as earlier commits move them")
{
	GitFixture fixture;

	fixture.Write("a.txt", "target\n");
	fixture.Commit("add the target line");

	fixture.Write("a.txt", "header\nheader\nheader\ntarget\n");
	fixture.Commit("insert three lines above");

	fixture.Write("a.txt", "header\nheader\nheader\nTARGET\n");
	fixture.Commit("edit the target line");

	const Repository repository = OpenRepositoryAt(fixture.GetPath());

	LogRequest request;
	const std::size_t kTargetLineToday = 4;
	request.File = std::filesystem::path("a.txt");
	request.Lines = Core::OneBasedLineRange{.First = kTargetLineToday, .Last = kTargetLineToday};

	const Core::Result<Core::LogPage> log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(Summaries(log.GetValue().Commits) == std::vector<std::string>{"edit the target line", "add the target line"});
}
