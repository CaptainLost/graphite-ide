#include <doctest/doctest.h>

#include "GitFixture.h"

#include <git2.h>

#include <string>
#include <vector>

namespace {

	std::vector<std::string> WalkHeadIds(const std::filesystem::path& root)
	{
		git_repository* repository = nullptr;
		REQUIRE(git_repository_open(&repository, Core::ToUtf8(root).c_str()) == 0);

		git_revwalk* walk = nullptr;
		REQUIRE(git_revwalk_new(&walk, repository) == 0);
		git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
		REQUIRE(git_revwalk_push_head(walk) == 0);

		std::vector<std::string> ids;
		git_oid id{};

		while (git_revwalk_next(&id, walk) == 0)
		{
			char text[GIT_OID_SHA1_HEXSIZE + 1] = {};
			git_oid_fmt(text, &id);
			ids.emplace_back(text, GIT_OID_SHA1_HEXSIZE);
		}

		git_revwalk_free(walk);
		git_repository_free(repository);
		return ids;
	}
}

TEST_CASE("The fixture builds a linear history newest first")
{
	GitFixture fixture;

	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("add a");

	fixture.Write("b.txt", "two\n");
	const std::string second = fixture.Commit("add b");

	const std::vector<std::string> ids = WalkHeadIds(fixture.GetPath());

	REQUIRE(ids.size() == 2);
	CHECK(ids[0] == second);
	CHECK(ids[1] == first);
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

	fixture.Checkout("main");
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

	char first[GIT_OID_SHA1_HEXSIZE + 1] = {};
	char second[GIT_OID_SHA1_HEXSIZE + 1] = {};
	git_oid_fmt(first, git_commit_parent_id(commit, 0));
	git_oid_fmt(second, git_commit_parent_id(commit, 1));

	CHECK(std::string(first, GIT_OID_SHA1_HEXSIZE) == mainTip);
	CHECK(std::string(second, GIT_OID_SHA1_HEXSIZE) == sideTip);

	git_commit_free(commit);
	git_repository_free(repository);
}
