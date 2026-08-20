#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <string>
#include <vector>

using Core::LogRequest;
using Core::Repository;

namespace {

	Repository OpenAt(const std::filesystem::path& root)
	{
		auto opened = Repository::Open(root);
		REQUIRE(opened.HasValue());
		return std::move(opened).GetValue();
	}

	std::vector<std::string> Summaries(const std::vector<Core::CommitSummary>& commits)
	{
		std::vector<std::string> text;
		text.reserve(commits.size());

		for (const Core::CommitSummary& commit : commits)
		{
			text.push_back(commit.Summary);
		}

		return text;
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

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("a.txt");

	auto log = repository.QueryLog(request);

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

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("new.txt");
	request.FollowRenames = true;

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());

	// Without following, the history would stop at "rename to new" and the commit that
	// created the file would be invisible — which is the case this whole feature exists for.
	CHECK(Summaries(log.GetValue().Commits)
		  == std::vector<std::string>{"edit new", "rename to new", "add old"});
}

TEST_CASE("A file's history stops at the rename when following is off")
{
	GitFixture fixture;

	fixture.Write("old.txt", "one\ntwo\nthree\nfour\n");
	fixture.Commit("add old");
	fixture.Rename("old.txt", "new.txt");
	fixture.Commit("rename to new");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("new.txt");
	request.FollowRenames = false;

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(Summaries(log.GetValue().Commits) == std::vector<std::string>{"rename to new"});
}

TEST_CASE("A file's history counts MaxCount in kept commits, not in walked ones")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("touch a once");

	for (int index = 0; index < 10; ++index)
	{
		fixture.Write("noise.txt", std::to_string(index) + "\n");
		fixture.Commit("noise " + std::to_string(index));
	}

	fixture.Write("a.txt", "two\n");
	fixture.Commit("touch a again");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("a.txt");
	request.MaxCount = 2;

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());

	// A limit applied to the walk instead of to the result would return nothing here:
	// the ten noise commits sit between the two that matter.
	CHECK(Summaries(log.GetValue().Commits) == std::vector<std::string>{"touch a again", "touch a once"});
}

TEST_CASE("The history of a path that never existed is empty rather than an error")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("add a");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("missing.txt");

	auto log = repository.QueryLog(request);

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

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("a.txt");
	request.Lines = Core::LineRange{1, 1};

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());

	// "edit the bottom" changed the same file and must not appear: the question was about
	// line one, and that is the whole point of asking by range rather than by file.
	CHECK(Summaries(log.GetValue().Commits) == std::vector<std::string>{"edit the top", "add a"});
}

TEST_CASE("A line range's history continues past a rename the rename commit did not touch")
{
	GitFixture fixture;

	fixture.Write("old.txt", "one\ntwo\nthree\nfour\n");
	fixture.Commit("add old");

	// Renamed with no content change, so the rename commit's diff carries no hunks at
	// all — TouchesRange has nothing to say yes to, which is exactly the case that used
	// to make the walk mistake the rename for "not touching the range" and stop dead.
	fixture.Rename("old.txt", "new.txt");
	fixture.Commit("rename to new");

	fixture.Write("new.txt", "ONE\ntwo\nthree\nfour\n");
	fixture.Commit("edit the top");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("new.txt");
	request.Lines = Core::LineRange{4, 4};
	request.FollowRenames = true;

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());

	// Line four was never touched by the rename or by "edit the top", which only changed
	// line one. Following the rename must still reach "add old", the commit that wrote
	// line four under the file's old name.
	CHECK(Summaries(log.GetValue().Commits) == std::vector<std::string>{"add old"});
}

TEST_CASE("A file's history walk stops at MaxWalk even though nothing else limits it")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("touch a");

	for (int index = 0; index < 20; ++index)
	{
		fixture.Write("noise.txt", std::to_string(index) + "\n");
		fixture.Commit("noise " + std::to_string(index));
	}

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("a.txt");
	request.MaxWalk = 5;

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());

	// The commit that touched a.txt sits at the bottom of a 21-commit history, past the
	// five-commit ceiling from HEAD. An unbounded walk finds it; a bounded one must not,
	// which is what proves the ceiling is bounding the walk and not just the result.
	CHECK(log.GetValue().Commits.empty());

	// An empty page and a finished page look identical unless the flag says otherwise -
	// without it, a caller would tell the user this file has no history, which is false.
	CHECK_FALSE(log.GetValue().Complete);
}

TEST_CASE("A file's history that runs out on its own reports the page as complete")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	fixture.Commit("touch a");

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("a.txt");

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());
	CHECK(Summaries(log.GetValue().Commits) == std::vector<std::string>{"touch a"});

	// The walk reached the end of history on its own, well inside every ceiling: nothing
	// was stopped early, so the page is the whole answer.
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

	const Repository repository = OpenAt(fixture.GetPath());

	LogRequest request;
	request.File = std::filesystem::path("a.txt");
	request.Lines = Core::LineRange{4, 4};

	auto log = repository.QueryLog(request);

	REQUIRE(log.HasValue());

	// The line is number four today and was number one when it was written. Without
	// mapping the range backwards, the walk would look at line four of a one-line file
	// and lose the commit that created it.
	CHECK(Summaries(log.GetValue().Commits)
		  == std::vector<std::string>{"edit the target line", "add the target line"});
}
