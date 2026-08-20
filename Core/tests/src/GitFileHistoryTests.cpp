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
	CHECK(Summaries(log.GetValue()) == std::vector<std::string>{"edit a", "add a"});
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
	CHECK(Summaries(log.GetValue())
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
	CHECK(Summaries(log.GetValue()) == std::vector<std::string>{"rename to new"});
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
	CHECK(Summaries(log.GetValue()) == std::vector<std::string>{"touch a again", "touch a once"});
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
	CHECK(log.GetValue().empty());
}
