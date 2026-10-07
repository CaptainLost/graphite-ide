#include <doctest/doctest.h>

#include "Git/GitFixture.h"

#include <git2.h>

#include <string>
#include <vector>

namespace {

	std::vector<std::string> WalkHeadIds(const std::filesystem::path& root)
	{
		git_repository* repository = nullptr;
		REQUIRE(git_repository_open(&repository, Core::ToUtf8(root).c_str()) == 0);

		git_revwalk* revisionWalk = nullptr;
		REQUIRE(git_revwalk_new(&revisionWalk, repository) == 0);
		git_revwalk_sorting(revisionWalk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
		REQUIRE(git_revwalk_push_head(revisionWalk) == 0);

		std::vector<std::string> commitIds;
		git_oid commitId{};

		while (git_revwalk_next(&commitId, revisionWalk) == 0)
		{
			commitIds.push_back(HexTextOf(commitId));
		}

		git_revwalk_free(revisionWalk);
		git_repository_free(repository);
		return commitIds;
	}
}

TEST_CASE("The fixture builds a linear history newest first")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	const std::string second = fixture.Commit("add b");

	const std::vector<std::string> commitIds = WalkHeadIds(fixture.GetPath());

	REQUIRE(commitIds.size() == 2);
	CHECK(commitIds[0] == second);
	CHECK(commitIds[1] == first);
}

TEST_CASE("The fixture builds a merge commit with two parents")
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

	const std::string merge = fixture.MergeCommit("merge side", "side");

	git_repository* repository = nullptr;
	REQUIRE(git_repository_open(&repository, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);

	git_oid mergeId{};
	REQUIRE(git_oid_fromstr(&mergeId, merge.c_str()) == 0);

	git_commit* commit = nullptr;
	REQUIRE(git_commit_lookup(&commit, repository, &mergeId) == 0);

	REQUIRE(git_commit_parentcount(commit) == 2);

	const unsigned int firstParentPosition = 0;
	const unsigned int secondParentPosition = 1;
	const std::string firstParent = HexTextOf(*git_commit_parent_id(commit, firstParentPosition));
	const std::string secondParent = HexTextOf(*git_commit_parent_id(commit, secondParentPosition));

	CHECK(firstParent == mainTip);
	CHECK(secondParent == sideTip);

	git_commit_free(commit);
	git_repository_free(repository);
}
