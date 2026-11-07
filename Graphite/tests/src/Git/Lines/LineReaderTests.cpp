#include <doctest/doctest.h>

#include "Git/Lines/LineReader.h"

#include "Git/Lines/GraphFile.h"
#include "Git/Lines/LineStore.h"
#include "Git/Tags/FirstTag.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Tag.h"

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

	// master (days 1..20) pushed to origin as its default, and a release branch from day 10 with six commits by three
	// e-mails on days 11..19, landed by the service, pushed as origin/release: the rules find release as a base.
	void BuildReleaseScenario(GitFixture& fixture)
	{
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
		releaseTip = fixture.CommitAtCommittedBy("release " + std::to_string(index), emails[index % 3], "noreply@example.com", days[index]);
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
	}

	constexpr std::int64_t kReleaseScenarioNow = 1767225600 + 30LL * 86400;

	// The id of HEAD's tree: an object that is not a commit.
	std::string TreeOfHead(const std::filesystem::path& root)
	{
		git_repository* raw = nullptr;
		REQUIRE(git_repository_open(&raw, Core::ToUtf8(root).c_str()) == 0);
		git_object* tree = nullptr;
		REQUIRE(git_revparse_single(&tree, raw, "HEAD^{tree}") == 0);
		const std::string hex = HexTextOf(*git_object_id(tree));
		git_object_free(tree);
		git_repository_free(raw);
		return hex;
	}

	void WriteAnswersText(const std::filesystem::path& file, const std::string& text)
	{
		std::filesystem::create_directories(file.parent_path());
		std::ofstream out{file, std::ios::binary | std::ios::trunc};
		out << text;
	}

	bool Holds(const std::vector<std::string>& names, const std::string& name)
	{
		return std::ranges::find(names, name) != names.end();
	}

	Core::Ref<const std::vector<Core::Branch>> BranchesOf(const Core::Repository& repository)
	{
		Core::Result<std::vector<Core::Branch>> branches = repository.QueryBranches();
		REQUIRE(branches.HasValue());
		return Core::CreateRef<const std::vector<Core::Branch>>(std::move(branches).GetValue());
	}

	constexpr std::int64_t kReadNow = 1800000000;

	constexpr std::int64_t kFirstDayStart = 1767225600;
	constexpr std::int64_t kSecondsPerDay = 86400;

	std::int64_t TimeOfDay(int day)
	{
		return kFirstDayStart + kSecondsPerDay * day;
	}

	// The commits of a tagged gitflow repository the tests ask about.
	struct TaggedGitflow
	{
		std::string FirstRelease;  // on release/1, below v1.0
		std::string SecondRelease; // on release/2, below v2.0-rc, which is older than v2.0
		std::string Merge;         // master's merge of release/2, held by v2.0 and by v2.0.1 after it
		std::string Hotfix;        // a deleted branch's commit, which only v2.0.1 reaches
		std::string DevelopTip;    // newer than every tag
	};

	// gitflow: develop from master's first commit, pushed as origin's default. release/1 and release/2 fork from
	// develop and are merged into master, release/1 back into develop too. master's merges are tagged v1.0 and v2.0
	// (annotated), release/2's tip v2.0-rc (lightweight, dated by its commit), and a hotfix on master v2.0.1 before the
	// hotfix branch was deleted.
	TaggedGitflow BuildTaggedGitflow(GitFixture& fixture)
	{
		TaggedGitflow built;

		fixture.Write("m.txt", "0");
		fixture.CommitAt("start", "ann@example.com", 1);
		fixture.Branch("develop");
		fixture.Checkout("develop");

		for (int day = 2; day <= 4; ++day)
		{
			fixture.Write("d.txt", std::to_string(day));
			fixture.CommitAt("develop " + std::to_string(day), "ann@example.com", day);
		}

		fixture.Branch("release/1");
		fixture.Checkout("release/1");
		fixture.Write("r.txt", "1a");
		built.FirstRelease = fixture.CommitAt("release 1a", "bob@example.com", 5);
		fixture.Write("r.txt", "1b");
		fixture.CommitAt("release 1b", "bob@example.com", 6);

		fixture.Checkout("master");
		const std::string firstMerge = fixture.MergeCommit("Merge release/1", "release/1");
		fixture.TagAnnotated("v1.0", firstMerge, TimeOfDay(7), "Version 1.0\n");

		fixture.Checkout("develop");
		fixture.MergeCommit("Merge release/1 into develop", "release/1");

		for (int day = 8; day <= 9; ++day)
		{
			fixture.Write("d.txt", std::to_string(day));
			fixture.CommitAt("develop " + std::to_string(day), "ann@example.com", day);
		}

		fixture.Branch("release/2");
		fixture.Checkout("release/2");
		fixture.Write("r.txt", "2a");
		built.SecondRelease = fixture.CommitAt("release 2a", "cat@example.com", 10);
		fixture.Write("r.txt", "2b");
		const std::string secondReleaseTip = fixture.CommitAt("release 2b", "cat@example.com", 11);
		fixture.TagLightweight("v2.0-rc", secondReleaseTip);

		fixture.Checkout("master");
		built.Merge = fixture.MergeCommit("Merge release/2", "release/2");
		fixture.TagAnnotated("v2.0", built.Merge, TimeOfDay(12), "Version 2.0\n");
		fixture.PushTo("master", built.Merge);

		fixture.Branch("hotfix");
		fixture.Checkout("hotfix");
		fixture.Write("m.txt", "fix");
		built.Hotfix = fixture.CommitAt("hotfix", "dan@example.com", 13);
		fixture.TagAnnotated("v2.0.1", built.Hotfix, TimeOfDay(13), "Version 2.0.1\n");

		fixture.Checkout("develop");
		fixture.Remove(".git/refs/heads/hotfix");

		for (int day = 14; day <= 16; ++day)
		{
			fixture.Write("d.txt", std::to_string(day));
			built.DevelopTip = fixture.CommitAt("develop " + std::to_string(day), "ann@example.com", day);
		}

		fixture.PushTo("develop", built.DevelopTip);
		fixture.SetRemoteHead("develop");
		return built;
	}

	constexpr std::int64_t kGitflowNow = kFirstDayStart + 30 * kSecondsPerDay;

	Core::Ref<const std::vector<Core::Tag>> TagsOf(const Core::Repository& repository)
	{
		Core::TagCache cache;
		Core::Result<std::vector<Core::Tag>> tags = repository.QueryTags(cache);
		REQUIRE(tags.HasValue());
		return Core::CreateRef<const std::vector<Core::Tag>>(std::move(tags).GetValue());
	}

	const Core::Tag* FirstTagOfHex(const LineRead& read, const std::string& hex)
	{
		return FirstTagOf(read, *Core::Oid::FromHex(hex));
	}

	// Every field of the two models, ref for ref, so a difference names what differs.
	void CheckSameModel(const LineRead& without, const LineRead& with)
	{
		REQUIRE(without.Refs.size() == with.Refs.size());
		for (std::size_t index = 0; index < without.Refs.size(); ++index)
		{
			CHECK(without.Refs[index].FullName == with.Refs[index].FullName);
			CHECK(without.Refs[index].Tip == with.Refs[index].Tip);
		}

		const Core::LineModel& a = without.Model;
		const Core::LineModel& b = with.Model;
		CHECK(a.MainLineGuessed == b.MainLineGuessed);

		REQUIRE(a.Bases.size() == b.Bases.size());
		for (std::size_t index = 0; index < a.Bases.size(); ++index)
		{
			INFO("base ", without.Refs[a.Bases[index].Ref].FullName);
			CHECK(a.Bases[index].Ref == b.Bases[index].Ref);
			CHECK(a.Bases[index].Reason == b.Bases[index].Reason);
			CHECK(a.Bases[index].Qualities == b.Bases[index].Qualities);
			CHECK(a.Bases[index].Active == b.Bases[index].Active);
		}

		REQUIRE(a.OldCopies.size() == b.OldCopies.size());
		for (std::size_t index = 0; index < a.OldCopies.size(); ++index)
		{
			CHECK(a.OldCopies[index].Ref == b.OldCopies[index].Ref);
			CHECK(a.OldCopies[index].Of == b.OldCopies[index].Of);
		}

		REQUIRE(a.Branches.size() == b.Branches.size());
		for (std::size_t index = 0; index < a.Branches.size(); ++index)
		{
			const Core::BranchLine& x = a.Branches[index];
			const Core::BranchLine& y = b.Branches[index];
			INFO("branch ", without.Refs[x.Ref].FullName);
			CHECK(x.Ref == y.Ref);
			CHECK(x.Kind == y.Kind);
			CHECK(x.ComparedWith == y.ComparedWith);
			CHECK(x.ByAnswer == y.ByAnswer);
			CHECK(x.AutomaticComparedWith == y.AutomaticComparedWith);
			CHECK(x.AutomaticKind == y.AutomaticKind);
			CHECK(x.State == y.State);
			CHECK(x.Ahead == y.Ahead);
			CHECK(x.SameRowAs == y.SameRowAs);
		}

		CHECK(without.Answers == with.Answers);
	}
}

TEST_CASE("Lines: a branch tracking a local branch names it under refs/heads")
{
	Core::Branch main;
	main.Name = "main";
	main.FullName = "refs/heads/main";

	Core::Branch topic;
	topic.Name = "topic";
	topic.FullName = "refs/heads/topic";
	topic.Upstream = "main";

	Core::Branch tracking;
	tracking.Name = "release";
	tracking.FullName = "refs/heads/release";
	tracking.Upstream = "origin/release";

	Core::Branch orphaned;
	orphaned.Name = "old";
	orphaned.FullName = "refs/heads/old";
	orphaned.Upstream = "origin/old";

	const std::vector<Core::LineRef> refs = LineRefsOf({main, topic, tracking, orphaned, RemoteBranch("origin/release")}, {"origin"});

	REQUIRE(refs.size() == 5);
	REQUIRE(refs[1].UpstreamFullName.has_value());
	CHECK(*refs[1].UpstreamFullName == "refs/heads/main");
	REQUIRE(refs[2].UpstreamFullName.has_value());
	CHECK(*refs[2].UpstreamFullName == "refs/remotes/origin/release");
	REQUIRE(refs[3].UpstreamFullName.has_value());
	CHECK(*refs[3].UpstreamFullName == "refs/remotes/origin/old");
}

TEST_CASE("Lines: a read hands back the graph its model was computed on")
{
	GitFixture fixture;
	BuildMasterAndFeature(fixture);
	TempDirectory store;
	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	for (int readNumber = 0; readNumber < 2; ++readNumber)
	{
		const Core::Result<LineRead> read = ReadLines(repository, StoreIn(store), 1800000000);
		REQUIRE(read.HasValue());
		const LineRead& value = read.GetValue();
		CHECK(value.Graph->Size() == 4);

		for (const Core::LineRef& ref : value.Refs)
		{
			CHECK(value.Graph->Find(ref.Tip) != Core::AncestryGraph::kNoCommit);
		}
	}
}

TEST_CASE("Lines: a reference to a missing object or to an object that is no commit does not stop the read")
{
	GitFixture fixture;
	BuildMasterAndFeature(fixture);
	fixture.Write(".git/refs/heads/broken", std::string(40, 'a') + "\n");
	fixture.Write(".git/refs/remotes/origin/not-a-commit", TreeOfHead(fixture.GetPath()) + "\n");
	TempDirectory store;
	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<LineRead> read = ReadLines(repository, StoreIn(store), 1800000000);

	INFO((read.HasValue() ? std::string{} : read.GetError().Message));
	REQUIRE(read.HasValue());
	CHECK(read.GetValue().CommitsRead == 4);

	const Core::BranchLine* broken = BranchNamed(read.GetValue(), "refs/heads/broken");
	REQUIRE(broken != nullptr);
	CHECK(broken->State == Core::BranchState::TipMissing);

	const Core::BranchLine* notACommit = BranchNamed(read.GetValue(), "refs/remotes/origin/not-a-commit");
	REQUIRE(notACommit != nullptr);
	CHECK(notACommit->State == Core::BranchState::TipMissing);

	const Core::BranchLine* feature = BranchNamed(read.GetValue(), "refs/heads/feature");
	REQUIRE(feature != nullptr);
	CHECK(feature->Ahead == 1);
}

TEST_CASE("Lines: a read drops kept bases and comparisons that name references gone from the repository")
{
	GitFixture fixture;
	BuildMasterAndFeature(fixture);
	TempDirectory store;
	const LineStorePaths paths = StoreIn(store);

	Core::LineAnswers answers;
	answers.KeptBases = {"refs/remotes/origin/gone", "refs/heads/feature"};
	answers.BaseOrder = {"refs/remotes/origin/gone", "refs/heads/feature"};
	answers.RemovedBases = {"refs/remotes/origin/gone-too"};
	answers.Comparisons["refs/heads/gone"] = "refs/remotes/origin/master";
	answers.Comparisons["refs/heads/feature"] = "refs/heads/gone";
	answers.Comparisons["refs/heads/master"] = std::nullopt;
	REQUIRE(SaveLineAnswers(paths.Answers, answers));

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<LineRead> read = ReadLines(repository, paths, 1800000000);
	REQUIRE(read.HasValue());

	const Core::LineAnswers saved = LoadLineAnswers(paths.Answers);
	CHECK(saved.KeptBases == std::vector<std::string>{"refs/heads/feature"});
	CHECK(saved.BaseOrder == std::vector<std::string>{"refs/heads/feature"});
	CHECK(saved.RemovedBases == std::vector<std::string>{"refs/remotes/origin/gone-too"});
	REQUIRE(saved.Comparisons.size() == 1);
	CHECK(saved.Comparisons.contains("refs/heads/master"));
	CHECK(saved == read.GetValue().Answers);
}

TEST_CASE("Lines: an answers file that cannot be read is not overwritten by a read")
{
	GitFixture fixture;
	BuildReleaseScenario(fixture);
	TempDirectory store;
	const LineStorePaths paths = StoreIn(store);
	const std::string typo = R"({"version": 1, "removedBases": ["refs/remotes/origin/staging"],})";
	WriteAnswersText(paths.Answers, typo);

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<LineRead> read = ReadLines(repository, paths, kReleaseScenarioNow);
	REQUIRE(read.HasValue());

	// The rules did find release, so an unguarded read would have saved it over the file.
	CHECK(std::ranges::find(read.GetValue().Answers.KeptBases, "refs/remotes/origin/release") != read.GetValue().Answers.KeptBases.end());
	CHECK(ReadWhole(paths.Answers) == typo);
}

TEST_CASE("Lines: a read's own changes to the answers are merged into what the file holds when it saves")
{
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "store" / "answers.json";

	SUBCASE("a base found by the rules joins an answer given while the read ran")
	{
		Core::LineAnswers meanwhile;
		meanwhile.RemovedBases = {"refs/remotes/origin/staging"};
		REQUIRE(SaveLineAnswers(file, meanwhile));

		Core::LineAnswers computed;
		computed.KeptBases = {"refs/remotes/origin/release"};

		const Core::LineAnswers result = SaveAnswerChanges(file, Core::LineAnswers{}, computed);

		Core::LineAnswers expected = meanwhile;
		expected.KeptBases = {"refs/remotes/origin/release"};
		CHECK(LoadLineAnswers(file) == expected);
		CHECK(result == expected);
	}

	SUBCASE("only what the read pruned is dropped")
	{
		Core::LineAnswers loaded;
		loaded.KeptBases = {"refs/remotes/origin/gone", "refs/remotes/origin/release"};
		loaded.BaseOrder = {"refs/remotes/origin/gone", "refs/remotes/origin/release"};
		loaded.Comparisons["refs/heads/gone"] = "refs/remotes/origin/main";
		loaded.Comparisons["refs/heads/kept"] = std::nullopt;

		Core::LineAnswers meanwhile = loaded;
		meanwhile.Comparisons["refs/heads/new"] = "refs/remotes/origin/release";
		meanwhile.MainLine = "refs/remotes/origin/main";
		meanwhile.AddedBases = {"refs/heads/staging"};
		meanwhile.BaseOrder.push_back("refs/heads/staging");
		REQUIRE(SaveLineAnswers(file, meanwhile));

		Core::LineAnswers computed = loaded;
		computed.KeptBases = {"refs/remotes/origin/release"};
		computed.BaseOrder = {"refs/remotes/origin/release"};
		computed.Comparisons.erase("refs/heads/gone");

		SaveAnswerChanges(file, loaded, computed);

		Core::LineAnswers expected = meanwhile;
		expected.KeptBases = {"refs/remotes/origin/release"};
		expected.BaseOrder = {"refs/remotes/origin/release", "refs/heads/staging"};
		expected.Comparisons.erase("refs/heads/gone");
		CHECK(LoadLineAnswers(file) == expected);
	}

	SUBCASE("a missing file is written")
	{
		Core::LineAnswers computed;
		computed.KeptBases = {"refs/remotes/origin/release"};

		SaveAnswerChanges(file, Core::LineAnswers{}, computed);

		CHECK(LoadLineAnswers(file) == computed);
	}

	SUBCASE("a file from a newer build is left alone")
	{
		const std::string newer = R"({"version": 2})";
		WriteAnswersText(file, newer);
		Core::LineAnswers computed;
		computed.KeptBases = {"refs/remotes/origin/release"};

		SaveAnswerChanges(file, Core::LineAnswers{}, computed);

		CHECK(ReadWhole(file) == newer);
	}

	SUBCASE("a file that cannot be read is left alone")
	{
		const std::string typo = R"({"version": 1, "keptBases": [})";
		WriteAnswersText(file, typo);
		Core::LineAnswers computed;
		computed.KeptBases = {"refs/remotes/origin/release"};

		SaveAnswerChanges(file, Core::LineAnswers{}, computed);

		CHECK(ReadWhole(file) == typo);
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

TEST_CASE("Lines: a cached head gone from the repository is passed over and the other heads still hide")
{
	GitFixture fixture;
	std::vector<std::string> commitIds;
	for (int index = 0; index < 4; ++index)
	{
		fixture.Write("a.txt", std::to_string(index));
		commitIds.push_back(fixture.Commit("old " + std::to_string(index)));
	}

	TempDirectory store;
	{
		const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
		REQUIRE(ReadLines(repository, StoreIn(store), 1800000000).HasValue());
	}

	// A branch deleted after the last read, its commit pruned by gc: a cached head git no longer has.
	Core::Oid ghostId;
	ghostId.Bytes.fill(0xAB);

	std::optional<Core::AncestryGraph> cached = LoadGraph(StoreIn(store).Graph);
	REQUIRE(cached.has_value());
	std::vector<Core::GraphCommit> commits = cached->ToCommits();
	commits.push_back(Core::GraphCommit{ghostId, {*Core::Oid::FromHex(commitIds[1])}, 1, "ghost@example.com", false});
	Core::AncestryGraph withGhost;
	withGhost.Append(std::move(commits));
	REQUIRE(SaveGraph(StoreIn(store).Graph, withGhost).HasValue());

	fixture.Write("a.txt", "new");
	const std::string fresh = fixture.Commit("new");

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<LineRead> read = ReadLines(repository, StoreIn(store), 1800000000);
	REQUIRE(read.HasValue());
	CHECK(read.GetValue().CommitsRead == 1);

	const std::optional<Core::AncestryGraph> saved = LoadGraph(StoreIn(store).Graph);
	REQUIRE(saved.has_value());
	CHECK(saved->Find(*Core::Oid::FromHex(fresh)) != Core::AncestryGraph::kNoCommit);
}

TEST_CASE("Lines: bases found by the rules are kept in the answers")
{
	GitFixture fixture;
	BuildReleaseScenario(fixture);

	TempDirectory store;
	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<LineRead> read = ReadLines(repository, StoreIn(store), kReleaseScenarioNow);
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
	BuildReleaseScenario(fixture);

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
	const Core::Result<LineRead> read = ReadLines(repository, paths, kReleaseScenarioNow);
	REQUIRE(read.HasValue());

	// The rules did find release, so an unguarded read would have saved it over the newer file.
	CHECK(std::ranges::find(read.GetValue().Answers.KeptBases, "refs/remotes/origin/release") != read.GetValue().Answers.KeptBases.end());
	CHECK(ReadWhole(paths.Answers) == newer);
}

TEST_CASE("Git window: a read appends newly found bases to the base order")
{
	// staging, one commit on master, was added by the user before this read; the rules find release.
	GitFixture fixture;
	BuildReleaseScenario(fixture);
	fixture.Branch("staging");
	fixture.Checkout("staging");
	fixture.Write("s.txt", "s");
	fixture.CommitAt("staging", "ann@example.com", 21);

	TempDirectory store;
	const LineStorePaths paths = StoreIn(store);
	Core::LineAnswers answers;
	answers.AddedBases = {"refs/heads/staging"};
	answers.BaseOrder = {"refs/heads/staging"};
	REQUIRE(SaveLineAnswers(paths.Answers, answers));

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<LineRead> read = ReadLines(repository, paths, kReleaseScenarioNow);
	REQUIRE(read.HasValue());

	const std::vector<std::string> expected = {"refs/heads/staging", "refs/remotes/origin/release"};
	const Core::LineAnswers saved = LoadLineAnswers(paths.Answers);
	CHECK(saved.BaseOrder == expected);
	CHECK(saved.KeptBases == std::vector<std::string>{"refs/remotes/origin/release"});
	CHECK(saved == read.GetValue().Answers);
}

TEST_CASE("Git window: a base the user stopped is never kept again")
{
	SUBCASE("stopped before the read")
	{
		GitFixture fixture;
		BuildReleaseScenario(fixture);
		TempDirectory store;
		const LineStorePaths paths = StoreIn(store);
		Core::LineAnswers answers;
		answers.RemovedBases = {"refs/remotes/origin/release"};
		REQUIRE(SaveLineAnswers(paths.Answers, answers));

		const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
		const Core::Result<LineRead> read = ReadLines(repository, paths, kReleaseScenarioNow);
		REQUIRE(read.HasValue());

		const Core::LineAnswers saved = LoadLineAnswers(paths.Answers);
		CHECK_FALSE(Holds(saved.KeptBases, "refs/remotes/origin/release"));
		CHECK_FALSE(Holds(saved.BaseOrder, "refs/remotes/origin/release"));
		CHECK(saved == read.GetValue().Answers);
	}

	SUBCASE("stopped while the read ran")
	{
		// The read found release; before it saved, the user stopped release.
		TempDirectory directory;
		const std::filesystem::path file = directory.GetPath() / "store" / "answers.json";
		Core::LineAnswers meanwhile;
		meanwhile.RemovedBases = {"refs/remotes/origin/release"};
		REQUIRE(SaveLineAnswers(file, meanwhile));

		Core::LineAnswers computed;
		computed.KeptBases = {"refs/remotes/origin/release"};
		computed.BaseOrder = {"refs/remotes/origin/release"};

		const Core::LineAnswers result = SaveAnswerChanges(file, Core::LineAnswers{}, computed);

		CHECK(LoadLineAnswers(file) == meanwhile);
		CHECK(result == meanwhile);
	}
}

TEST_CASE("Git window: a base order entry added while a read ran survives its save")
{
	// The user added staging while a read that found release ran.
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "store" / "answers.json";
	Core::LineAnswers meanwhile;
	meanwhile.AddedBases = {"refs/heads/staging"};
	meanwhile.BaseOrder = {"refs/heads/staging"};
	REQUIRE(SaveLineAnswers(file, meanwhile));

	Core::LineAnswers computed;
	computed.KeptBases = {"refs/remotes/origin/release"};
	computed.BaseOrder = {"refs/remotes/origin/release"};

	const Core::LineAnswers result = SaveAnswerChanges(file, Core::LineAnswers{}, computed);

	Core::LineAnswers expected = meanwhile;
	expected.KeptBases = {"refs/remotes/origin/release"};
	expected.BaseOrder = {"refs/heads/staging", "refs/remotes/origin/release"};
	CHECK(LoadLineAnswers(file) == expected);
	CHECK(result == expected);
}

TEST_CASE("Git window: a read reuses the graph it is given")
{
	GitFixture fixture;
	BuildMasterAndFeature(fixture);
	TempDirectory store;
	const LineStorePaths paths = StoreIn(store);

	Core::Ref<const Core::AncestryGraph> given;
	{
		const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
		const Core::Result<LineRead> first = ReadLines(repository, paths, LineReadInput{.Branches = BranchesOf(repository), .Now = kReadNow});
		REQUIRE(first.HasValue());
		given = first.GetValue().Graph;
	}

	// Without the cache file only the graph given holds the four commits read before.
	REQUIRE(std::filesystem::remove(paths.Graph));
	fixture.Write("b.txt", "g");
	const std::string newer = fixture.Commit("more feature work");

	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Result<LineRead> second = ReadLines(repository, paths, LineReadInput{.Branches = BranchesOf(repository), .Graph = given, .Now = kReadNow});
	REQUIRE(second.HasValue());
	const LineRead& read = second.GetValue();

	CHECK(read.CommitsRead == 1);
	CHECK(read.Graph != given);
	CHECK(read.Graph->Size() == 5);
	CHECK(read.Graph->Find(*Core::Oid::FromHex(newer)) != Core::AncestryGraph::kNoCommit);
	CHECK(given->Size() == 4);
	REQUIRE(read.Generations != nullptr);
	CHECK(read.Generations->size() == 5);

	const Core::BranchLine* feature = BranchNamed(read, "refs/heads/feature");
	REQUIRE(feature != nullptr);
	CHECK(feature->Ahead == 2);

	const std::optional<Core::AncestryGraph> saved = LoadGraph(paths.Graph);
	REQUIRE(saved.has_value());
	CHECK(saved->Size() == 5);
}

TEST_CASE("Git window: a read of the same tips appends nothing and keeps the graph pointer")
{
	GitFixture fixture;
	BuildMasterAndFeature(fixture);
	TempDirectory store;
	const LineStorePaths paths = StoreIn(store);
	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<LineRead> first = ReadLines(repository, paths, LineReadInput{.Branches = BranchesOf(repository), .Now = kReadNow});
	REQUIRE(first.HasValue());
	const LineRead& earlier = first.GetValue();
	REQUIRE(earlier.Generations != nullptr);
	CHECK(earlier.Generations->size() == earlier.Graph->Size());

	const Core::Ref<const std::vector<Core::Branch>> snapshot = BranchesOf(repository);
	const Core::Result<LineRead> second = ReadLines(
		repository, paths, LineReadInput{.Branches = snapshot, .Graph = earlier.Graph, .Now = kReadNow, .Generations = earlier.Generations});
	REQUIRE(second.HasValue());
	const LineRead& later = second.GetValue();

	CHECK(later.CommitsRead == 0);
	CHECK(later.Graph == earlier.Graph);
	CHECK(later.Generations == earlier.Generations);
	CHECK(SameModel(earlier.Model, later.Model));

	// The read describes the snapshot it was given, ref for branch, with the remotes beside it.
	CHECK(later.Branches == snapshot);
	REQUIRE(later.Refs.size() == snapshot->size());
	for (std::size_t index = 0; index < later.Refs.size(); ++index)
	{
		CHECK(later.Refs[index].FullName == (*snapshot)[index].FullName);
	}

	REQUIRE(later.Remotes.size() == 1);
	CHECK(later.Remotes.front().Name == "origin");
	CHECK_FALSE(later.Remotes.front().Url.empty());
}

TEST_CASE("Line read: a tag's commit no branch reaches is read into the graph")
{
	GitFixture fixture;
	const TaggedGitflow built = BuildTaggedGitflow(fixture);
	const Core::Oid hotfix = *Core::Oid::FromHex(built.Hotfix);
	TempDirectory store;
	const LineStorePaths paths = StoreIn(store);
	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<LineRead> untagged = ReadLines(repository, paths, LineReadInput{.Branches = BranchesOf(repository), .Now = kGitflowNow});
	REQUIRE(untagged.HasValue());
	CHECK(untagged.GetValue().Graph->Find(hotfix) == Core::AncestryGraph::kNoCommit);
	CHECK(untagged.GetValue().Tags == nullptr);

	// From the graph the read before left in the store: only what the tags reach and no branch does is read.
	const Core::Ref<const std::vector<Core::Tag>> tags = TagsOf(repository);
	const Core::Result<LineRead> tagged =
		ReadLines(repository, paths, LineReadInput{.Branches = BranchesOf(repository), .Tags = tags, .Now = kGitflowNow});
	REQUIRE(tagged.HasValue());
	const LineRead& read = tagged.GetValue();

	CHECK(read.CommitsRead == 1);
	CHECK(read.Graph->Find(hotfix) != Core::AncestryGraph::kNoCommit);
	CHECK(read.Graph->Size() == untagged.GetValue().Graph->Size() + 1);
	CHECK(read.Tags == tags);

	// The tags are no refs of the model.
	CHECK(read.Refs.size() == read.Branches->size());
	for (const Core::LineRef& ref : read.Refs)
	{
		CHECK_FALSE(ref.FullName.starts_with(Core::kTagPrefix));
	}

	const std::optional<Core::AncestryGraph> saved = LoadGraph(paths.Graph);
	REQUIRE(saved.has_value());
	CHECK(saved->Find(hotfix) != Core::AncestryGraph::kNoCommit);
}

TEST_CASE("Line read: the model is the same with and without tags")
{
	GitFixture fixture;
	BuildTaggedGitflow(fixture);
	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());
	const Core::Ref<const std::vector<Core::Branch>> branches = BranchesOf(repository);
	const Core::Ref<const std::vector<Core::Tag>> tags = TagsOf(repository);
	REQUIRE(tags->size() == 4);

	TempDirectory withoutStore;
	TempDirectory withStore;
	const Core::Result<LineRead> without = ReadLines(repository, StoreIn(withoutStore), LineReadInput{.Branches = branches, .Now = kGitflowNow});
	const Core::Result<LineRead> with = ReadLines(repository, StoreIn(withStore), LineReadInput{.Branches = branches, .Tags = tags, .Now = kGitflowNow});
	REQUIRE(without.HasValue());
	REQUIRE(with.HasValue());
	REQUIRE(with.GetValue().Graph->Size() > without.GetValue().Graph->Size());

	CheckSameModel(without.GetValue(), with.GetValue());
}

TEST_CASE("Line read: the first tag of a commit is the oldest tag holding it")
{
	GitFixture fixture;
	const TaggedGitflow built = BuildTaggedGitflow(fixture);
	TempDirectory store;
	const Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	const Core::Result<LineRead> tagged =
		ReadLines(repository, StoreIn(store), LineReadInput{.Branches = BranchesOf(repository), .Tags = TagsOf(repository), .Now = kGitflowNow});
	REQUIRE(tagged.HasValue());
	const LineRead& read = tagged.GetValue();
	REQUIRE(read.FirstTag.size() == read.Graph->Size());

	const Core::Tag* firstRelease = FirstTagOfHex(read, built.FirstRelease);
	REQUIRE(firstRelease != nullptr);
	CHECK(firstRelease->Name == "v1.0");

	// v2.0 holds it too, but release/2's own tag was made before it.
	const Core::Tag* secondRelease = FirstTagOfHex(read, built.SecondRelease);
	REQUIRE(secondRelease != nullptr);
	CHECK(secondRelease->Name == "v2.0-rc");

	const Core::Tag* merge = FirstTagOfHex(read, built.Merge);
	REQUIRE(merge != nullptr);
	CHECK(merge->Name == "v2.0");

	const Core::Tag* hotfix = FirstTagOfHex(read, built.Hotfix);
	REQUIRE(hotfix != nullptr);
	CHECK(hotfix->Name == "v2.0.1");

	CHECK(FirstTagOfHex(read, built.DevelopTip) == nullptr);
}
