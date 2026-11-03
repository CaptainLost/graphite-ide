#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <string>
#include <vector>

namespace {

	std::vector<std::string> SummariesOf(const Core::LogPage& page)
	{
		std::vector<std::string> summaries;

		for (const Core::CommitSummary& commit : page.Commits)
		{
			summaries.push_back(commit.Summary);
		}

		return summaries;
	}

	Core::Oid TipOf(const Core::Repository& repository, const std::string& reference)
	{
		const Core::Result<Core::Oid> tip = repository.ResolveReference(reference);
		REQUIRE(tip.HasValue());

		return tip.GetValue();
	}
}

TEST_CASE("Excluding the fork point lists only the branch's own commits newest first")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("fork point");
	fixture.Branch("side");
	fixture.Branch("fork");

	fixture.Write("a.txt", "master\n");
	fixture.Commit("master only");

	fixture.Checkout("side");
	fixture.Write("b.txt", "first\n");
	fixture.Commit("side first");
	fixture.Write("b.txt", "second\n");
	fixture.Commit("side second");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::LogRequest request;
	request.Ref = "refs/heads/side";
	request.Exclude = TipOf(repository, "refs/heads/fork");

	const Core::Result<Core::LogPage> page = repository.QueryLog(request);
	REQUIRE(page.HasValue());

	CHECK(SummariesOf(page.GetValue()) == std::vector<std::string>{"side second", "side first"});
	CHECK(page.GetValue().Complete);
}

TEST_CASE("Excluding the tip itself leaves an empty and complete page")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::LogRequest request;
	request.Ref = "refs/heads/master";
	request.Exclude = TipOf(repository, "refs/heads/master");

	const Core::Result<Core::LogPage> page = repository.QueryLog(request);
	REQUIRE(page.HasValue());

	CHECK(page.GetValue().Commits.empty());
	CHECK(page.GetValue().Complete);
}

TEST_CASE("Excluding a commit the repository does not have is NotFound")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	Core::Oid unknownCommit;
	unknownCommit.Bytes[0] = 0xAB;

	Core::LogRequest request;
	request.Ref = "refs/heads/master";
	request.Exclude = unknownCommit;

	const Core::Result<Core::LogPage> page = repository.QueryLog(request);

	REQUIRE_FALSE(page.HasValue());
	CHECK(page.GetError().Code == Core::ErrorCode::NotFound);
}
