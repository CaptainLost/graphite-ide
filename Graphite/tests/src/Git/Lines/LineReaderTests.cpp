#include <doctest/doctest.h>

#include "Git/Lines/LineReader.h"

#include "Git/Lines/GraphFile.h"
#include "Git/Lines/LineStore.h"

#include "Core/FileSystem/FileSystem.h"

#include "GitFixture.h"
#include "TempDirectory.h"

#include <algorithm>
#include <fstream>
#include <iterator>

using namespace Graphite;

namespace {
	Core::Branch RemoteBranch(const std::string& name)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/remotes/" + name;
		branch.Kind = Core::RefKind::Remote;
		return branch;
	}

	LineStorePaths StoreIn(const TempDirectory& directory)
	{
		return LineStoreFor(directory.GetPath() / "repo" / ".git", directory.GetPath() / "store");
	}

	const Core::BranchLine* BranchNamed(const LineRead& read, const std::string& fullName)
	{
		for (const Core::BranchLine& line : read.Model.Branches)
		{
			if (read.Refs[line.Ref].FullName == fullName)
			{
				return &line;
			}
		}

		return nullptr;
	}

	bool SameModel(const Core::LineModel& a, const Core::LineModel& b)
	{
		if (a.Bases.size() != b.Bases.size() || a.Branches.size() != b.Branches.size() || a.MainLineGuessed != b.MainLineGuessed)
		{
			return false;
		}

		for (std::size_t i = 0; i < a.Bases.size(); ++i)
		{
			if (a.Bases[i].Ref != b.Bases[i].Ref || a.Bases[i].Reason != b.Bases[i].Reason)
			{
				return false;
			}
		}

		for (std::size_t i = 0; i < a.Branches.size(); ++i)
		{
			const Core::BranchLine& x = a.Branches[i];
			const Core::BranchLine& y = b.Branches[i];

			if (x.Ref != y.Ref || x.Kind != y.Kind || x.ComparedWith != y.ComparedWith || x.State != y.State || x.Ahead != y.Ahead)
			{
				return false;
			}
		}

		return true;
	}

	std::string ReadWhole(const std::filesystem::path& file)
	{
		std::ifstream stream{file, std::ios::binary};
		return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	}

	// master with three commits pushed to origin as its default, and a local feature with one commit.
	void BuildMasterAndFeature(GitFixture& fixture)
	{
		fixture.Write("a.txt", "1");
		fixture.Commit("one");
		fixture.Write("a.txt", "2");
		fixture.Commit("two");
		fixture.Write("a.txt", "3");
		const std::string third = fixture.Commit("three");
		fixture.PushTo("master", third);
		fixture.SetRemoteHead("master");

		fixture.Branch("feature");
		fixture.Checkout("feature");
		fixture.Write("b.txt", "f");
		fixture.Commit("feature work");
	}
}

TEST_CASE("Lines: remote branches belong to the longest remote name that prefixes them")
{
	const std::vector<Core::Branch> branches = {RemoteBranch("team/fork/main"), RemoteBranch("team/main"), RemoteBranch("origin/x")};

	const std::vector<Core::LineRef> refs = LineRefsOf(branches, {"origin", "team", "team/fork"});

	REQUIRE(refs.size() == 3);
	CHECK(refs[0].Remote == "team/fork");
	CHECK(refs[0].ShortName == "main");
	CHECK(refs[1].Remote == "team");
	CHECK(refs[1].ShortName == "main");
	CHECK(refs[2].Remote == "origin");
	CHECK(refs[2].ShortName == "x");
	CHECK(refs[0].FullName == "refs/remotes/team/fork/main");
}

TEST_CASE("Lines: a local upstream becomes a full remote reference name")
{
	Core::Branch local;
	local.Name = "feature";
	local.FullName = "refs/heads/feature";
	local.Upstream = "origin/feature";

	const std::vector<Core::LineRef> refs = LineRefsOf({local}, {"origin"});

	REQUIRE(refs.size() == 1);
	CHECK(refs[0].Remote.empty());
	CHECK(refs[0].ShortName == "feature");
	REQUIRE(refs[0].UpstreamFullName.has_value());
	CHECK(*refs[0].UpstreamFullName == "refs/remotes/origin/feature");
}

TEST_CASE("Lines: the first read fills the cache and the second reads nothing")
{
	GitFixture fixture;
	BuildMasterAndFeature(fixture);
	TempDirectory store;
	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<LineRead> first = ReadLines(repository, StoreIn(store), 1800000000);
	REQUIRE(first.HasValue());
	CHECK(first.GetValue().CommitsRead == 4);

	const Core::Result<LineRead> second = ReadLines(repository, StoreIn(store), 1800000000);
	REQUIRE(second.HasValue());
	CHECK(second.GetValue().CommitsRead == 0);
	CHECK(SameModel(first.GetValue().Model, second.GetValue().Model));

	const LineRead& read = first.GetValue();
	REQUIRE_FALSE(read.Model.Bases.empty());
	CHECK(read.Refs[read.Model.Bases.front().Ref].FullName == "refs/remotes/origin/master");
	CHECK(read.Model.Bases.front().Reason == Core::BaseReason::ServerDefault);

	const Core::BranchLine* feature = BranchNamed(read, "refs/heads/feature");
	REQUIRE(feature != nullptr);
	REQUIRE(feature->ComparedWith.has_value());
	CHECK(read.Refs[*feature->ComparedWith].FullName == "refs/remotes/origin/master");
	CHECK(feature->Ahead == 1);
}

TEST_CASE("Lines: a new commit is the only thing read after the cache")
{
	GitFixture fixture;
	BuildMasterAndFeature(fixture);
	TempDirectory store;

	{
		const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
		REQUIRE(ReadLines(repository, StoreIn(store), 1800000000).HasValue());
		REQUIRE(ReadLines(repository, StoreIn(store), 1800000000).HasValue());
	}

	fixture.Write("b.txt", "g");
	fixture.Commit("more feature work");

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<LineRead> third = ReadLines(repository, StoreIn(store), 1800000000);
	REQUIRE(third.HasValue());
	CHECK(third.GetValue().CommitsRead == 1);

	const Core::BranchLine* feature = BranchNamed(third.GetValue(), "refs/heads/feature");
	REQUIRE(feature != nullptr);
	CHECK(feature->Ahead == 2);
}

TEST_CASE("Lines: new commits on a single branch are read without the history below them")
{
	GitFixture fixture;
	for (int index = 0; index < 20; ++index)
	{
		fixture.Write("a.txt", std::to_string(index));
		fixture.Commit("old " + std::to_string(index));
	}

	TempDirectory store;
	{
		const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
		const Core::Result<LineRead> first = ReadLines(repository, StoreIn(store), 1800000000);
		REQUIRE(first.HasValue());
		CHECK(first.GetValue().CommitsRead == 20);
	}

	for (int index = 0; index < 3; ++index)
	{
		fixture.Write("a.txt", "new" + std::to_string(index));
		fixture.Commit("new " + std::to_string(index));
	}

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<LineRead> second = ReadLines(repository, StoreIn(store), 1800000000);
	REQUIRE(second.HasValue());
	CHECK(second.GetValue().CommitsRead == 3);
}

TEST_CASE("Lines: bases found by the rules are kept in the answers")
{
	GitFixture fixture;

	for (int day = 1; day <= 10; ++day)
	{
		fixture.Write("m.txt", std::to_string(day));
		fixture.CommitAt("master " + std::to_string(day), "ann@example.com", day);
	}

	fixture.Branch("release");
	fixture.Checkout("release");

	const char* emails[] = {"a@example.com", "b@example.com", "c@example.com"};
	const int days[] = {11, 12, 14, 16, 18, 19};
	std::string releaseTip;
	for (int index = 0; index < 6; ++index)
	{
		fixture.Write("r.txt", std::to_string(index));
		releaseTip = fixture.CommitAt("release " + std::to_string(index), emails[index % 3], days[index]);
	}
	fixture.PushTo("release", releaseTip);

	fixture.Checkout("master");
	std::string masterTip;
	for (int day = 11; day <= 20; ++day)
	{
		fixture.Write("m.txt", std::to_string(day));
		masterTip = fixture.CommitAt("master " + std::to_string(day), "ann@example.com", day);
	}
	fixture.PushTo("master", masterTip);
	fixture.SetRemoteHead("master");

	TempDirectory store;
	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const std::int64_t now = 1767225600 + 30LL * 86400;

	const Core::Result<LineRead> read = ReadLines(repository, StoreIn(store), now);
	REQUIRE(read.HasValue());

	const Core::LineAnswers saved = LoadLineAnswers(StoreIn(store).Answers);
	CHECK(std::ranges::find(saved.KeptBases, "refs/remotes/origin/release") != saved.KeptBases.end());
	CHECK(saved == read.GetValue().Answers);
}

TEST_CASE("Lines: a deepened shallow clone is read whole again")
{
	GitFixture fixture;
	fixture.Write("a.txt", "1");
	const std::string first = fixture.Commit("one");
	fixture.Write("a.txt", "2");
	const std::string second = fixture.Commit("two");
	fixture.Write("a.txt", "3");
	fixture.Commit("three");

	fixture.MarkShallow(second);

	TempDirectory store;
	{
		const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
		REQUIRE(repository.IsShallow());

		const Core::Result<LineRead> read = ReadLines(repository, StoreIn(store), 1800000000);
		REQUIRE(read.HasValue());
		CHECK(read.GetValue().CommitsRead == 2);
	}

	const std::optional<Core::AncestryGraph> cut = LoadGraph(StoreIn(store).Graph);
	REQUIRE(cut.has_value());
	const Core::AncestryGraph::Index cutIndex = cut->Find(*Core::Oid::FromHex(second));
	REQUIRE(cutIndex != Core::AncestryGraph::kNoCommit);
	CHECK(cut->ParentsCutAt(cutIndex));

	fixture.Remove(".git/shallow");

	const Core::Repository deepened = OpenRepositoryAt(fixture.GetPath());
	REQUIRE_FALSE(deepened.IsShallow());

	const Core::Result<LineRead> again = ReadLines(deepened, StoreIn(store), 1800000000);
	REQUIRE(again.HasValue());
	CHECK(again.GetValue().CommitsRead == 3);

	const std::optional<Core::AncestryGraph> whole = LoadGraph(StoreIn(store).Graph);
	REQUIRE(whole.has_value());
	const Core::AncestryGraph::Index wholeIndex = whole->Find(*Core::Oid::FromHex(second));
	REQUIRE(wholeIndex != Core::AncestryGraph::kNoCommit);
	CHECK_FALSE(whole->ParentsCutAt(wholeIndex));
	CHECK(whole->ParentsOf(wholeIndex).size() == 1);
	CHECK(whole->Find(*Core::Oid::FromHex(first)) != Core::AncestryGraph::kNoCommit);
}

TEST_CASE("Lines: an answers file from a newer build is not overwritten by a read")
{
	GitFixture fixture;
	BuildMasterAndFeature(fixture);

	TempDirectory store;
	const LineStorePaths paths = StoreIn(store);
	const std::string newer = R"({"version": 2, "somethingNew": true})";
	std::filesystem::create_directories(paths.Answers.parent_path());
	{
		std::ofstream out{paths.Answers, std::ios::binary};
		out << newer;
	}
	REQUIRE(HasForeignVersion(paths.Answers));

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<LineRead> read = ReadLines(repository, paths, 1800000000);
	REQUIRE(read.HasValue());

	CHECK(ReadWhole(paths.Answers) == newer);
}
