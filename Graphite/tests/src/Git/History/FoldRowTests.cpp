#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "Git/Lines/LineReadFixtures.h"

#include "Git/History/FoldRow.h"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <unordered_set>

using Graphite::BranchLines;
using Graphite::FoldRow;
using Graphite::HistoryLine;
using Graphite::LineFold;
using Graphite::LineRead;
using Graphite::Fixtures::BranchSpec;
using Graphite::Fixtures::LineReadBuilder;

namespace {

	Core::LogPage PageOf(std::initializer_list<std::uint8_t> firstBytes, bool complete = true)
	{
		Core::LogPage page;
		page.Complete = complete;

		for (const std::uint8_t firstByte : firstBytes)
		{
			Core::CommitSummary commit;
			commit.Id = OidWithFirstByte(firstByte);
			page.Commits.push_back(commit);
		}

		return page;
	}

	std::unordered_set<Core::Oid> SetOf(std::initializer_list<std::uint8_t> firstBytes)
	{
		std::unordered_set<Core::Oid> commits;

		for (const std::uint8_t firstByte : firstBytes)
		{
			commits.insert(OidWithFirstByte(firstByte));
		}

		return commits;
	}

	// develop is a base; feature is compared with it; the case sets feature's state. lone is compared with nothing.
	struct Scene
	{
		LineRead Read;
		std::size_t Develop = 0;
		std::size_t Feature = 0;
		std::size_t Lone = 0;

		Core::BranchLine& FeatureLine() { return Read.Model.Branches.front(); }

		HistoryLine LineOf(const std::string& ref, bool newerShown = false) const
		{
			const BranchLines lines(Read);
			return Graphite::HistoryLineOf(Read, lines, ref, newerShown);
		}
	};

	Scene MakeScene(Core::BranchState featureState = Core::BranchState::NotMerged)
	{
		LineReadBuilder builder;

		Scene scene;
		scene.Develop = builder.AddLocal("develop");
		builder.MakeBase(scene.Develop);
		scene.Feature = builder.AddLocal("feature/x", BranchSpec{.State = featureState});
		builder.Compare(scene.Feature, scene.Develop);
		scene.Lone = builder.AddLocal("lone", BranchSpec{.State = Core::BranchState::NotCompared});
		scene.Read = builder.Build();

		scene.FeatureLine().Kind = Core::ComparedKind::Base;

		return scene;
	}
}

TEST_CASE("Git window: the fold row sits before the first commit that is not the branch's own")
{
	const Core::LogPage page = PageOf({1, 2, 3, 4});
	const Core::LogPage own = PageOf({1, 2});

	const std::optional<FoldRow> fold = Graphite::FindFoldRow(page, own, "develop", 80);

	REQUIRE(fold.has_value());
	CHECK(fold->BeforeCommit == 2);
	CHECK(fold->Text == "80 newer commits on develop since this branch forked");
	CHECK(fold->Action == "Show them");
}

TEST_CASE("Git window: no fold row when nothing is newer on the line")
{
	const Core::LogPage page = PageOf({1, 2, 3, 4});
	const Core::LogPage own = PageOf({1, 2});

	CHECK_FALSE(Graphite::FindFoldRow(page, own, "develop", 0).has_value());
}

TEST_CASE("Git window: the fold row's count reads an ellipsis until measured")
{
	const Core::LogPage page = PageOf({1, 2, 3});
	const Core::LogPage own = PageOf({1});

	const std::optional<FoldRow> fold = Graphite::FindFoldRow(page, own, "origin/main", std::nullopt);

	REQUIRE(fold.has_value());
	CHECK(fold->Text == "\xE2\x80\xA6 newer commits on origin/main since this branch forked");
}

TEST_CASE("Git window: the fold row counts one newer commit in the singular and many with a thousands comma")
{
	const Core::LogPage page = PageOf({1, 2});
	const Core::LogPage own = PageOf({1});

	CHECK(Graphite::FindFoldRow(page, own, "develop", 1).value_or(FoldRow{}).Text ==
		  "1 newer commit on develop since this branch forked");
	CHECK(Graphite::FindFoldRow(page, own, "develop", 3'448).value_or(FoldRow{}).Text ==
		  "3,448 newer commits on develop since this branch forked");
}

TEST_CASE("Git window: a branch with no commits of its own folds its line above its first row")
{
	const Core::LogPage page = PageOf({3, 4});
	const Core::LogPage own = PageOf({});

	const std::optional<FoldRow> fold = Graphite::FindFoldRow(page, own, "develop", 5);

	REQUIRE(fold.has_value());
	CHECK(fold->BeforeCommit == 0);
}

TEST_CASE("Git window: no fold row on a page of own commits only or with own commits cut short")
{
	CHECK_FALSE(Graphite::FindFoldRow(PageOf({1, 2}, false), PageOf({1, 2, 3}), "develop", 5).has_value());
	CHECK_FALSE(Graphite::FindFoldRow(PageOf({}), PageOf({}), "develop", 5).has_value());
	CHECK_FALSE(Graphite::FindFoldRow(PageOf({1, 2, 3}), PageOf({1}, false), "develop", 5).has_value());
}

TEST_CASE("Git window: commits merged in from the line come under the fold row even when interleaved")
{
	const std::optional<FoldRow> fold = Graphite::FindFoldRow(PageOf({1, 9, 2, 3}), PageOf({1, 2}), "develop", 5);

	REQUIRE(fold.has_value());
	CHECK(fold->BeforeCommit == 1);
}

TEST_CASE("Git window: a branch that is not merged folds its line until the newer commits are shown")
{
	const Scene scene = MakeScene();

	const HistoryLine folded = scene.LineOf("refs/heads/feature/x");
	CHECK(folded.Fold == LineFold::Folded);
	CHECK(folded.Branch == "refs/heads/feature/x");
	CHECK(folded.Line == "refs/heads/develop");
	CHECK(folded.LineName == "develop");
	CHECK(folded.BranchTip == scene.Read.Refs[scene.Feature].Tip);
	CHECK(folded.LineTip == scene.Read.Refs[scene.Develop].Tip);

	CHECK(scene.LineOf("refs/heads/feature/x", true).Fold == LineFold::Shown);
}

TEST_CASE("Git window: a merged branch and one with no commits of its own show their line combined")
{
	CHECK(MakeScene(Core::BranchState::MergedByHistory).LineOf("refs/heads/feature/x").Fold == LineFold::Combined);
	CHECK(MakeScene(Core::BranchState::NoOwnCommits).LineOf("refs/heads/feature/x").Fold == LineFold::Combined);
}

TEST_CASE("Git window: a base and a branch without a line show the ref alone")
{
	const Scene scene = MakeScene();

	CHECK(scene.LineOf("refs/heads/develop").Fold == LineFold::None);
	CHECK(scene.LineOf("refs/heads/lone").Fold == LineFold::None);
	CHECK(scene.LineOf("refs/heads/gone").Fold == LineFold::None);

	for (const Core::BranchState state :
		 {Core::BranchState::NoCommonHistory, Core::BranchState::HistoryCutOff, Core::BranchState::TipMissing})
	{
		CHECK(MakeScene(state).LineOf("refs/heads/feature/x").Fold == LineFold::None);
	}
}

TEST_CASE("Git window: a base's local copy with no commits of its own is shown alone as the base is")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");

	const std::size_t base = builder.AddRemoteBranch("origin", "develop");
	builder.MakeBase(base);
	const std::size_t copy = builder.AddLocal("develop", BranchSpec{.State = Core::BranchState::NoOwnCommits});
	builder.SameRow(copy, base);
	builder.Compare(copy, base);

	Scene scene;
	scene.Read = builder.Build();
	scene.FeatureLine().Kind = Core::ComparedKind::Base;

	CHECK(scene.LineOf("refs/heads/develop").Fold == LineFold::None);
}

TEST_CASE("Git window: a folded history asks the branch alone and its own commits")
{
	const Scene scene = MakeScene();
	const HistoryLine line = scene.LineOf("refs/heads/feature/x");

	Core::LogRequest log;
	log.FirstParentOnly = true;
	Graphite::WidenToLine(log, line);

	CHECK(log.AlsoFrom.empty());

	const std::optional<Core::LogRequest> own = Graphite::MarkedQueryFor(line, log);

	REQUIRE(own.has_value());
	CHECK(own->Ref == std::optional<std::string>{"refs/heads/feature/x"});
	CHECK(own->Exclude == std::optional<Core::Oid>{line.LineTip});
	CHECK(own->FirstParentOnly);
}

TEST_CASE("Git window: a shown history walks the line too and marks its newer commits")
{
	const Scene scene = MakeScene();
	const HistoryLine line = scene.LineOf("refs/heads/feature/x", true);

	Core::LogRequest log;
	Graphite::WidenToLine(log, line);

	REQUIRE(log.AlsoFrom.size() == 1);
	CHECK(log.AlsoFrom.front() == line.LineTip);

	const std::optional<Core::LogRequest> newer = Graphite::MarkedQueryFor(line, log);

	REQUIRE(newer.has_value());
	CHECK(newer->Ref == std::optional<std::string>{"refs/heads/develop"});
	CHECK(newer->Exclude == std::optional<Core::Oid>{line.BranchTip});
	CHECK_FALSE(newer->FirstParentOnly);
}

TEST_CASE("Git window: a ref alone asks no marked commits and does not walk a line")
{
	const Scene scene = MakeScene();
	const HistoryLine line = scene.LineOf("refs/heads/develop");

	Core::LogRequest log;
	Graphite::WidenToLine(log, line);

	CHECK(log.AlsoFrom.empty());
	CHECK_FALSE(Graphite::MarkedQueryFor(line, log).has_value());
}

TEST_CASE("Git window: a history narrowed by words still marks the line's newer commits but not the branch's own")
{
	const Scene scene = MakeScene();

	Core::LogRequest log;
	log.Filter.Text = "encoding";

	const std::optional<Core::LogRequest> newer = Graphite::MarkedQueryFor(scene.LineOf("refs/heads/feature/x", true), log);

	REQUIRE(newer.has_value());
	CHECK(newer->Ref == std::optional<std::string>{"refs/heads/develop"});
	CHECK(newer->Filter.Text.empty());

	CHECK_FALSE(Graphite::MarkedQueryFor(scene.LineOf("refs/heads/feature/x"), log).has_value());

	Core::LogRequest spaces;
	spaces.Filter.Text = "   ";
	CHECK(Graphite::MarkedQueryFor(scene.LineOf("refs/heads/feature/x"), spaces).has_value());
}

TEST_CASE("Git window: the rows of a folded history wait for the fold row's count")
{
	const Scene scene = MakeScene();
	const HistoryLine folded = scene.LineOf("refs/heads/feature/x");
	const Core::LogRequest log;

	CHECK(Graphite::RowsWaitForFoldCount(folded, log, std::nullopt));
	CHECK_FALSE(Graphite::RowsWaitForFoldCount(folded, log, 0));
	CHECK_FALSE(Graphite::RowsWaitForFoldCount(folded, log, 80));
}

TEST_CASE("Git window: rows with no fold row to count do not wait for a count")
{
	const Scene scene = MakeScene();
	const Core::LogRequest log;

	CHECK_FALSE(Graphite::RowsWaitForFoldCount(scene.LineOf("refs/heads/feature/x", true), log, std::nullopt));
	CHECK_FALSE(Graphite::RowsWaitForFoldCount(scene.LineOf("refs/heads/develop"), log, std::nullopt));
	CHECK_FALSE(Graphite::RowsWaitForFoldCount(MakeScene(Core::BranchState::MergedByHistory).LineOf("refs/heads/feature/x"),
											   log, std::nullopt));

	Core::LogRequest words;
	words.Filter.Text = "encoding";
	CHECK_FALSE(Graphite::RowsWaitForFoldCount(scene.LineOf("refs/heads/feature/x"), words, std::nullopt));
}

TEST_CASE("Git window: the marked commits are looked up at least as far as the page reaches")
{
	const Scene scene = MakeScene();

	Core::LogRequest log;
	log.MaxCount = 20'000;
	log.MaxWalk = 400'000;

	const std::optional<Core::LogRequest> own = Graphite::MarkedQueryFor(scene.LineOf("refs/heads/feature/x"), log);

	REQUIRE(own.has_value());
	CHECK(own->MaxCount == 20'000);
	CHECK(own->MaxWalk == 400'000);
}

TEST_CASE("Git window: an opened history starts on its first own commit")
{
	const std::unordered_set<Core::Oid> own = SetOf({2, 3});

	CHECK(Graphite::StartingCommit(PageOf({9, 2, 3}), &own, nullptr) == std::optional<Core::Oid>{OidWithFirstByte(2)});
}

TEST_CASE("Git window: an opened history with the line shown starts on its first commit that is not newer")
{
	const std::unordered_set<Core::Oid> newer = SetOf({7, 8});

	CHECK(Graphite::StartingCommit(PageOf({7, 8, 1, 2}), nullptr, &newer) == std::optional<Core::Oid>{OidWithFirstByte(1)});
}

TEST_CASE("Git window: an opened history without marks or without such a commit starts on its first row")
{
	const std::unordered_set<Core::Oid> none;

	CHECK(Graphite::StartingCommit(PageOf({4, 5}), nullptr, nullptr) == std::optional<Core::Oid>{OidWithFirstByte(4)});
	CHECK(Graphite::StartingCommit(PageOf({4, 5}), &none, nullptr) == std::optional<Core::Oid>{OidWithFirstByte(4)});
	CHECK_FALSE(Graphite::StartingCommit(PageOf({}), nullptr, nullptr).has_value());
}

TEST_CASE("Git window: a base with a pick folds its history against the pick, and shows it when asked")
{
	Scene scene = MakeScene();
	scene.Read.Answers.Comparisons["refs/heads/develop"] = "refs/heads/feature/x";

	const HistoryLine folded = scene.LineOf("refs/heads/develop");
	CHECK(folded.Fold == LineFold::Folded);
	CHECK(folded.Branch == "refs/heads/develop");
	CHECK(folded.Line == "refs/heads/feature/x");
	CHECK(folded.LineName == "feature/x");
	CHECK(folded.BranchTip == scene.Read.Refs[scene.Develop].Tip);
	CHECK(folded.LineTip == scene.Read.Refs[scene.Feature].Tip);

	CHECK(scene.LineOf("refs/heads/develop", true).Fold == LineFold::Shown);
}

TEST_CASE("Git window: an old copy with a pick folds its history against the pick")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddRemote("fork");

	const std::size_t develop = builder.AddRemoteBranch("origin", "develop");
	builder.MakeBase(develop);
	const std::size_t release = builder.AddRemoteBranch("origin", "release");
	builder.MakeBase(release);
	const std::size_t copy = builder.AddRemoteBranch("fork", "develop");
	builder.Compare(copy, develop);

	Scene scene;
	scene.Read = builder.Build();
	scene.Read.Model.OldCopies.push_back(Core::OldCopy{.Ref = copy, .Of = develop});

	CHECK(scene.LineOf("refs/remotes/fork/develop").Fold == LineFold::None);

	scene.FeatureLine().ByAnswer = true;
	scene.FeatureLine().ComparedWith = release;
	scene.FeatureLine().Kind = Core::ComparedKind::Base;
	scene.FeatureLine().State = Core::BranchState::MergedByHistory;

	const HistoryLine picked = scene.LineOf("refs/remotes/fork/develop");
	CHECK(picked.Fold == LineFold::Folded);
	CHECK(picked.Line == "refs/remotes/origin/release");
	CHECK(picked.BranchTip == scene.Read.Refs[copy].Tip);
}
