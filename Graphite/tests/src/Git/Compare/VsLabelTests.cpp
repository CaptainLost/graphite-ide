#include <doctest/doctest.h>

#include "Git/Compare/HistoryBanner.h"
#include "Git/Compare/VsLabel.h"
#include "Git/Content/ContentMark.h"
#include "Git/Lines/LineReadFixtures.h"

#include <optional>
#include <string>

using Graphite::BranchLines;
using Graphite::DescribeVs;
using Graphite::LineRead;
using Graphite::VsInput;
using Graphite::VsLabel;
using Graphite::Fixtures::BranchSpec;
using Graphite::Fixtures::LineReadBuilder;

namespace {

	// develop (a base) and feature compared with it; the model's line for feature is edited by each case.
	struct Scene
	{
		LineRead Read;
		std::size_t Develop = 0;
		std::size_t Feature = 0;

		Core::BranchLine& Line() { return Read.Model.Branches.front(); }
	};

	Scene MakeScene()
	{
		LineReadBuilder builder;
		builder.AddRemote("origin");

		Scene scene;
		scene.Develop = builder.AddLocal("develop");
		builder.MakeBase(scene.Develop);
		scene.Feature = builder.AddLocal("feature/x");
		builder.Compare(scene.Feature, scene.Develop);
		scene.Read = builder.Build();

		scene.Line().Kind = Core::ComparedKind::Base;
		scene.Line().Ahead = 6;

		return scene;
	}

	VsLabel Describe(const Scene& scene, const std::string& ref, std::optional<Core::LineDistance> distance = std::nullopt)
	{
		const BranchLines lines(scene.Read);

		return DescribeVs(VsInput{scene.Read, lines, ref, distance});
	}
}

TEST_CASE("Git window: a branch on a base reads vs the base with its counts")
{
	Scene scene = MakeScene();

	const VsLabel label =
		Describe(scene, "refs/heads/feature/x", Core::LineDistance{.Ahead = 6, .Behind = 80, .ForkTime = std::nullopt, .ForkOnLine = true});

	CHECK(label.Lead == "vs");
	CHECK(label.Line == "develop");
	CHECK(label.Counts == "6 ahead, 80 behind");
	CHECK_FALSE(label.Mark.has_value());
}

TEST_CASE("Git window: behind reads an ellipsis until it is measured")
{
	Scene scene = MakeScene();

	const VsLabel pending = Describe(scene, "refs/heads/feature/x");
	CHECK(pending.Counts == "6 ahead, \xE2\x80\xA6 behind");

	const VsLabel measured = Describe(
		scene, "refs/heads/feature/x", Core::LineDistance{.Ahead = 6, .Behind = 3'448, .ForkTime = std::nullopt, .ForkOnLine = true});
	CHECK(measured.Counts == "6 ahead, 3,448 behind");
}

TEST_CASE("Git window: a branch built on another branch reads on top of it")
{
	Scene scene = MakeScene();
	scene.Line().Kind = Core::ComparedKind::Branch;

	const VsLabel label = Describe(scene, "refs/heads/feature/x");

	CHECK(label.Lead == "on top of");
	CHECK(label.Line == "develop");
}

TEST_CASE("Git window: a branch merged by history reads merged into its line with no counts")
{
	Scene scene = MakeScene();
	scene.Line().State = Core::BranchState::MergedByHistory;

	const VsLabel label = Describe(scene, "refs/heads/feature/x");

	CHECK(label.Lead == "merged into");
	CHECK(label.Line == "develop");
	CHECK(label.Counts.empty());
}

TEST_CASE("Git window: a branch with no own commits reads that it points into its line")
{
	Scene scene = MakeScene();
	scene.Line().State = Core::BranchState::NoOwnCommits;

	const VsLabel label = Describe(scene, "refs/heads/feature/x");

	CHECK(label.Lead == "no own commits \xC2\xB7 points into");
	CHECK(label.Line == "develop");
	CHECK(label.Counts.empty());
}

TEST_CASE("Git window: a base shown reads compare with")
{
	Scene scene = MakeScene();

	const VsLabel label = Describe(scene, "refs/heads/develop");

	CHECK(label.Lead == "compare with\xE2\x80\xA6");
	CHECK(label.Line.empty());
	CHECK(label.Counts.empty());
}

TEST_CASE("Git window: a base's local copy with no commits of its own reads compare with as the base does")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");

	Scene scene;
	scene.Develop = builder.AddRemoteBranch("origin", "develop");
	builder.MakeBase(scene.Develop);
	const std::size_t copy = builder.AddLocal("develop", BranchSpec{.State = Core::BranchState::NoOwnCommits});
	builder.SameRow(copy, scene.Develop);
	builder.Compare(copy, scene.Develop);
	scene.Read = builder.Build();
	scene.Line().Kind = Core::ComparedKind::Base;

	CHECK(Describe(scene, "refs/heads/develop").Lead == "compare with\xE2\x80\xA6");

	scene.Line().ByAnswer = true;
	CHECK(Describe(scene, "refs/heads/develop").Lead == "no own commits \xC2\xB7 points into");

	scene.Line().ByAnswer = false;
	scene.Line().State = Core::BranchState::NotMerged;
	CHECK(Describe(scene, "refs/heads/develop").Lead == "vs");
}

TEST_CASE("Git window: a branch compared with nothing by the user's answer says so and is marked as a pick")
{
	Scene scene = MakeScene();
	scene.Line().Kind = Core::ComparedKind::None;
	scene.Line().ComparedWith.reset();
	scene.Line().ByAnswer = true;

	const VsLabel label = Describe(scene, "refs/heads/feature/x");

	CHECK(label.Lead == "compared with nothing");
	CHECK(label.Mark == std::optional<std::string>{"your pick"});
}

TEST_CASE("Git window: the states of an odd history each get words")
{
	Scene scene = MakeScene();

	scene.Line().State = Core::BranchState::HistoryCutOff;
	CHECK(Describe(scene, "refs/heads/feature/x").Lead == "history cut off");

	scene.Line().State = Core::BranchState::NoCommonHistory;
	CHECK(Describe(scene, "refs/heads/feature/x").Lead == "no common history");

	scene.Line().State = Core::BranchState::TipMissing;
	CHECK(Describe(scene, "refs/heads/feature/x").Lead == "tip missing");
}

TEST_CASE("Git window: a picked line is marked your pick")
{
	Scene scene = MakeScene();
	scene.Line().ByAnswer = true;

	CHECK(Describe(scene, "refs/heads/feature/x").Mark == std::optional<std::string>{"your pick"});
}

TEST_CASE("Git window: the guessed main line is marked guessed")
{
	Scene scene = MakeScene();
	scene.Read.Model.Bases.front().Reason = Core::BaseReason::MainLineGuess;

	CHECK(Describe(scene, "refs/heads/feature/x").Mark == std::optional<std::string>{"guessed"});
}

TEST_CASE("Git window: a remote line keeps its remote in its name")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");

	const std::size_t develop = builder.AddRemoteBranch("origin", "develop");
	builder.MakeBase(develop);
	const std::size_t feature = builder.AddLocal("feature");
	builder.Compare(feature, develop);

	Scene scene;
	scene.Read = builder.Build();
	scene.Line().Kind = Core::ComparedKind::Base;

	const VsLabel label = Describe(scene, "refs/heads/feature");

	CHECK(label.Line == "origin/develop");
}

TEST_CASE("Git window: a reference the read does not know has nothing to say")
{
	Scene scene = MakeScene();

	const VsLabel label = Describe(scene, "refs/heads/gone");

	CHECK(label.Lead.empty());
}

TEST_CASE("Git window: the operation in progress is said in words")
{
	CHECK(Graphite::HeadOperationText(Core::RepositoryOperation::None).empty());
	CHECK(Graphite::HeadOperationText(Core::RepositoryOperation::Rebase) == "rebase in progress");
	CHECK(Graphite::HeadOperationText(Core::RepositoryOperation::Merge) == "merge in progress");
	CHECK(Graphite::HeadOperationText(Core::RepositoryOperation::CherryPick) == "cherry-pick in progress");
	CHECK(Graphite::HeadOperationText(Core::RepositoryOperation::Revert) == "revert in progress");
	CHECK(Graphite::HeadOperationText(Core::RepositoryOperation::Bisect) == "bisect in progress");
	CHECK(Graphite::HeadOperationText(Core::RepositoryOperation::ApplyMailbox) == "applying patches");
}

TEST_CASE("Git window: the header keeps the words it drew for a reference while their distance is measured again")
{
	const Graphite::HeaderWords drawn{.Label = VsLabel{.Lead = "vs", .Line = "main", .Counts = "6 ahead, 80 behind", .How = "", .Mark = std::nullopt},
									  .Banner = std::nullopt};
	const Graphite::HeaderWords pending{.Label = VsLabel{.Lead = "vs", .Line = "main", .Counts = "6 ahead, \xE2\x80\xA6 behind", .Mark = std::nullopt},
										.Banner = std::nullopt};
	const Graphite::HeaderWords landed{.Label = VsLabel{.Lead = "vs", .Line = "main", .Counts = "6 ahead, 81 behind", .How = "", .Mark = std::nullopt},
									   .Banner = std::nullopt};
	const std::optional<Graphite::DrawnHeaderWords> held =
		Graphite::DrawnHeaderWords{.ShownRef = "refs/heads/feature/x", .Words = drawn};

	CHECK(Graphite::HeaderWordsToDraw(pending, true, "refs/heads/feature/x", held).Label.Counts == "6 ahead, 80 behind");
	CHECK(Graphite::HeaderWordsToDraw(landed, false, "refs/heads/feature/x", held).Label.Counts == "6 ahead, 81 behind");

	// Another reference's words never stand in.
	CHECK(Graphite::HeaderWordsToDraw(pending, true, "refs/heads/other", held).Label.Counts == "6 ahead, \xE2\x80\xA6 behind");
	CHECK(Graphite::HeaderWordsToDraw(pending, true, "refs/heads/feature/x", std::nullopt).Label.Counts ==
		  "6 ahead, \xE2\x80\xA6 behind");
}

TEST_CASE("Git window: the header keeps the words it drew while the rows under them are the page of the line before")
{
	// The user picked a line for an old copy: the banner the words had goes, and the fold row comes with the page asked
	// with the pick, so both wait for that page.
	const Graphite::HeaderWords drawn{.Label = VsLabel{.Lead = "compared with nothing", .Line = "", .Counts = "", .How = "", .Mark = std::nullopt},
									  .Banner = "The default branch of origin, made when the fork was."};
	const Graphite::HeaderWords picked{.Label = VsLabel{.Lead = "vs", .Line = "upstream/develop", .Counts = "0 ahead, 8,913 behind", .How = "", .Mark = "your pick"},
									   .Banner = std::nullopt};
	const std::optional<Graphite::DrawnHeaderWords> held = Graphite::DrawnHeaderWords{.ShownRef = "refs/heads/develop", .Words = drawn};

	constexpr bool kMeasured = false;
	constexpr bool kContentKnown = false;
	constexpr bool kPageOfLineBefore = false;
	constexpr bool kPageOfLineShown = true;

	CHECK(Graphite::HeaderWordsWait(kMeasured, kPageOfLineBefore, kContentKnown));
	CHECK(Graphite::HeaderWordsToDraw(picked, Graphite::HeaderWordsWait(kMeasured, kPageOfLineBefore, kContentKnown), "refs/heads/develop", held)
			  .Banner.has_value());

	CHECK_FALSE(Graphite::HeaderWordsWait(kMeasured, kPageOfLineShown, kContentKnown));
	CHECK_FALSE(Graphite::HeaderWordsToDraw(picked, Graphite::HeaderWordsWait(kMeasured, kPageOfLineShown, kContentKnown), "refs/heads/develop", held)
					.Banner.has_value());

	// A distance measured again waits whatever page is in hand.
	CHECK(Graphite::HeaderWordsWait(true, kPageOfLineShown, kContentKnown));
}

namespace {

	// develop and release are bases, fork/develop an old copy of develop; the case picks what they are compared with.
	struct PickScene
	{
		LineRead Read;
		std::size_t Develop = 0;
		std::size_t Release = 0;
		std::size_t Copy = 0;
	};

	PickScene MakePickScene()
	{
		LineReadBuilder builder;
		builder.AddRemote("origin");
		builder.AddRemote("fork");

		PickScene scene;
		scene.Develop = builder.AddRemoteBranch("origin", "develop");
		builder.MakeBase(scene.Develop);
		scene.Release = builder.AddRemoteBranch("origin", "release");
		builder.MakeBase(scene.Release);
		scene.Copy = builder.AddRemoteBranch("fork", "develop");
		builder.Compare(scene.Copy, scene.Develop);
		scene.Read = builder.Build();
		scene.Read.Model.OldCopies.push_back(Core::OldCopy{.Ref = scene.Copy, .Of = scene.Develop});

		return scene;
	}

	VsLabel DescribePick(const PickScene& scene, const std::string& ref, std::optional<Core::LineDistance> distance)
	{
		const BranchLines lines(scene.Read);

		return DescribeVs(VsInput{scene.Read, lines, ref, distance});
	}
}

TEST_CASE("Git window: a base with a pick reads vs the pick with both counts measured and your pick")
{
	PickScene scene = MakePickScene();
	scene.Read.Answers.Comparisons["refs/remotes/origin/release"] = "refs/remotes/origin/develop";

	const VsLabel pending = DescribePick(scene, "refs/remotes/origin/release", std::nullopt);
	CHECK(pending.Lead == "vs");
	CHECK(pending.Line == "origin/develop");
	CHECK(pending.Counts == "\xE2\x80\xA6 ahead, \xE2\x80\xA6 behind");
	CHECK(pending.Mark == std::optional<std::string>{"your pick"});

	const VsLabel measured = DescribePick(scene, "refs/remotes/origin/release",
										  Core::LineDistance{.Ahead = 1'200, .Behind = 7, .ForkTime = std::nullopt, .ForkOnLine = true});
	CHECK(measured.Counts == "1,200 ahead, 7 behind");
}

TEST_CASE("Git window: an old copy with a pick reads vs the pick rather than compared with nothing")
{
	PickScene scene = MakePickScene();
	CHECK(DescribePick(scene, "refs/remotes/fork/develop", std::nullopt).Lead == "compared with nothing");

	Core::BranchLine& copy = scene.Read.Model.Branches.back();
	copy.ByAnswer = true;
	copy.ComparedWith = scene.Release;
	copy.Kind = Core::ComparedKind::Base;

	const VsLabel label = DescribePick(scene, "refs/remotes/fork/develop",
									   Core::LineDistance{.Ahead = 2, .Behind = 30, .ForkTime = std::nullopt, .ForkOnLine = true});
	CHECK(label.Lead == "vs");
	CHECK(label.Line == "origin/release");
	CHECK(label.Counts == "2 ahead, 30 behind");
	CHECK(label.Mark == std::optional<std::string>{"your pick"});
}

namespace {

	VsLabel DescribeWithContent(Scene& scene, const std::string& ref, Core::ContentKind kind)
	{
		scene.Line().State = Core::BranchState::NotMerged;

		const BranchLines lines(scene.Read);
		const Core::ContentVerdict verdict{.Outcome = Core::ContentOutcome::Merged, .Kind = kind, .Landing = std::nullopt, .Twins = {}};

		return DescribeVs(VsInput{scene.Read, lines, ref, std::nullopt, verdict});
	}
}

TEST_CASE("Git window: a branch merged by a squash is labelled merged into its line with squash")
{
	Scene scene = MakeScene();

	const VsLabel label = DescribeWithContent(scene, "refs/heads/feature/x", Core::ContentKind::Squash);

	CHECK(label.Lead == "merged into");
	CHECK(label.Line == "develop");
	CHECK(label.How == "squash");
	CHECK(label.Counts.empty());
}

TEST_CASE("Git window: a branch merged by a rebase is labelled merged into its line with rebase")
{
	Scene scene = MakeScene();

	const VsLabel label = DescribeWithContent(scene, "refs/heads/feature/x", Core::ContentKind::Rebase);

	CHECK(label.Lead == "merged into");
	CHECK(label.Line == "develop");
	CHECK(label.How == "rebase");
}

TEST_CASE("Git window: a branch the check found not merged keeps its counts")
{
	Scene scene = MakeScene();
	scene.Line().State = Core::BranchState::NotMerged;

	const BranchLines lines(scene.Read);
	const Core::ContentVerdict notMerged{.Outcome = Core::ContentOutcome::NotMerged, .Kind = Core::ContentKind::Squash, .Landing = std::nullopt, .Twins = {}};
	const VsLabel label = DescribeVs(VsInput{scene.Read, lines, "refs/heads/feature/x", std::nullopt, notMerged});

	CHECK(label.Lead == "vs");
	CHECK(label.How.empty());
	CHECK(label.Counts == "6 ahead, \xE2\x80\xA6 behind");
}

TEST_CASE("Git window: the header words wait for the shown branch's content check")
{
	const Graphite::HeaderWords drawn{.Label = VsLabel{.Lead = "vs", .Line = "main", .Counts = "6 ahead, 80 behind", .How = "", .Mark = std::nullopt},
									  .Banner = std::nullopt};
	const Graphite::HeaderWords merged{.Label = VsLabel{.Lead = "merged into", .Line = "main", .Counts = "", .How = "squash", .Mark = std::nullopt},
									   .Banner = "Merged into main by a squash: its changes are in main, its commits are not."};
	const std::optional<Graphite::DrawnHeaderWords> held =
		Graphite::DrawnHeaderWords{.ShownRef = "refs/heads/feature/x", .Words = drawn};

	const Graphite::ContentMark pending;

	// The distance is in and the page is the line's, yet the words stay what they were until the check answers.
	const bool waits = Graphite::HeaderWordsWait(false, true, Graphite::ContentCheckWaits(pending, true));

	CHECK(waits);
	CHECK(Graphite::HeaderWordsToDraw(merged, waits, "refs/heads/feature/x", held).Label.How.empty());

	const Graphite::ContentMark known{.State = Graphite::ContentMark::State::Known, .Verdict = Core::ContentVerdict{}};

	CHECK_FALSE(Graphite::HeaderWordsWait(false, true, Graphite::ContentCheckWaits(known, true)));
}

TEST_CASE("Git window: an unknown content check releases the header words")
{
	const Graphite::ContentMark unknown{.State = Graphite::ContentMark::State::Unknown, .Verdict = std::nullopt};
	const Graphite::ContentMark pending;

	CHECK_FALSE(Graphite::ContentCheckWaits(unknown, true));
	CHECK_FALSE(Graphite::HeaderWordsWait(false, true, Graphite::ContentCheckWaits(unknown, true)));

	// The model checks nothing while the window is off screen: a pending mark then holds nothing back.
	CHECK_FALSE(Graphite::ContentCheckWaits(pending, false));
}
