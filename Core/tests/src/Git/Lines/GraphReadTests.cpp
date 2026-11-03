#include <doctest/doctest.h>

#include "Core/Git/OwnAuthors.h"
#include "Core/Git/Repository.h"

#include "GitFixture.h"
#include "Git/WorktreeFixture.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

	Core::Oid OidFromHex(const std::string& hex)
	{
		const std::optional<Core::Oid> id = Core::Oid::FromHex(hex);
		REQUIRE(id.has_value());
		return *id;
	}

	const Core::GraphCommit* FindCommit(const std::vector<Core::GraphCommit>& commits, const std::string& hex)
	{
		const Core::Oid id = OidFromHex(hex);
		const auto found = std::find_if(commits.begin(), commits.end(),
										[&](const Core::GraphCommit& commit) { return commit.Id == id; });
		return found == commits.end() ? nullptr : &*found;
	}
}

TEST_CASE("Lines: reading the graph returns every reachable commit with its parents in order")
{
	GitFixture repo;
	repo.Write("a.txt", "1");
	const std::string first = repo.Commit("first");
	repo.Branch("side");
	repo.Write("a.txt", "2");
	const std::string second = repo.Commit("second");
	repo.Checkout("side");
	repo.Write("b.txt", "1");
	const std::string onSide = repo.CommitAs("side", "Sam", "Sam@Example.com ");
	repo.Checkout("master");
	const std::string merge = repo.MergeCommit("merge", "side");
	const Core::Repository repository = OpenRepositoryAt(repo.GetPath());

	const auto read = repository.ReadGraphCommits({{OidFromHex(merge)}, {}});

	REQUIRE(read.HasValue());
	const std::vector<Core::GraphCommit>& commits = read.GetValue();
	CHECK(commits.size() == 4);

	const Core::GraphCommit* mergeCommit = FindCommit(commits, merge);
	REQUIRE(mergeCommit != nullptr);
	REQUIRE(mergeCommit->Parents.size() == 2);
	CHECK(mergeCommit->Parents[0] == OidFromHex(second));
	CHECK(mergeCommit->Parents[1] == OidFromHex(onSide));
	CHECK_FALSE(mergeCommit->ParentsCut);

	const Core::GraphCommit* sideCommit = FindCommit(commits, onSide);
	REQUIRE(sideCommit != nullptr);
	CHECK(sideCommit->AuthorKey == Core::AuthorKeyOf("Sam@Example.com "));
	CHECK(sideCommit->Time > 0);

	const Core::GraphCommit* firstCommit = FindCommit(commits, first);
	REQUIRE(firstCommit != nullptr);
	CHECK(firstCommit->Parents.empty());
}

TEST_CASE("Lines: known tips and their ancestors are not read again")
{
	GitFixture repo;
	repo.Write("a.txt", "1");
	repo.Commit("first");
	repo.Write("a.txt", "2");
	const std::string second = repo.Commit("second");
	repo.Write("a.txt", "3");
	const std::string third = repo.Commit("third");
	const Core::Repository repository = OpenRepositoryAt(repo.GetPath());

	const auto read = repository.ReadGraphCommits({{OidFromHex(third)}, {OidFromHex(second)}});

	REQUIRE(read.HasValue());
	REQUIRE(read.GetValue().size() == 1);
	CHECK(read.GetValue().front().Id == OidFromHex(third));
}

TEST_CASE("Lines: a shallow boundary comes back cut and without parents")
{
	GitFixture repo;
	repo.Write("a.txt", "1");
	repo.Commit("first");
	repo.Write("a.txt", "2");
	const std::string second = repo.Commit("second");
	repo.Write("a.txt", "3");
	const std::string third = repo.Commit("third");

	{
		const Core::Repository whole = OpenRepositoryAt(repo.GetPath());
		CHECK_FALSE(whole.IsShallow());
	}

	repo.MarkShallow(second);
	const Core::Repository repository = OpenRepositoryAt(repo.GetPath());
	CHECK(repository.IsShallow());

	const auto read = repository.ReadGraphCommits({{OidFromHex(third)}, {}});

	REQUIRE(read.HasValue());
	const std::vector<Core::GraphCommit>& commits = read.GetValue();
	CHECK(commits.size() == 2);

	const Core::GraphCommit* boundary = FindCommit(commits, second);
	REQUIRE(boundary != nullptr);
	CHECK(boundary->ParentsCut);
	CHECK(boundary->Parents.empty());

	const Core::GraphCommit* tip = FindCommit(commits, third);
	REQUIRE(tip != nullptr);
	CHECK_FALSE(tip->ParentsCut);
	CHECK(tip->Parents.size() == 1);
}

TEST_CASE("Lines: remote names are listed in name order")
{
	GitFixture repo;
	repo.AddRemote("team");
	repo.AddRemote("team/fork");
	repo.AddRemote("origin");
	const Core::Repository repository = OpenRepositoryAt(repo.GetPath());

	const auto names = repository.QueryRemoteNames();

	REQUIRE(names.HasValue());
	CHECK(names.GetValue() == std::vector<std::string>{"origin", "team", "team/fork"});
}

TEST_CASE("Lines: the common directory is shared by the main checkout and a linked worktree")
{
	GitFixture repo;
	repo.Write("a.txt", "1");
	repo.Commit("first");
	repo.Branch("side");
	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(repo, "side", worktreePath);

	const Core::Repository main = OpenRepositoryAt(repo.GetPath());
	const Core::Repository linked = OpenRepositoryAt(worktreePath);

	std::error_code error;
	CHECK(std::filesystem::equivalent(main.CommonDirectory(), linked.CommonDirectory(), error));
	CHECK_FALSE(error);
	CHECK_FALSE(main.CommonDirectory().empty());
}
