#include <doctest/doctest.h>

#include "Git/Compare/VsLabel.h"
#include "Git/Lines/LineReadFixtures.h"

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

TEST_CASE("Git window: the room for the counts is the same before and after the distance lands")
{
	Scene scene = MakeScene();

	const VsLabel pending = Describe(scene, "refs/heads/feature/x");
	const VsLabel landed = Describe(
		scene, "refs/heads/feature/x", Core::LineDistance{.Ahead = 6, .Behind = 80, .ForkTime = std::nullopt, .ForkOnLine = true});
	const VsLabel thousands = Describe(
		scene, "refs/heads/feature/x", Core::LineDistance{.Ahead = 6, .Behind = 3'448, .ForkTime = std::nullopt, .ForkOnLine = true});

	CHECK_FALSE(pending.CountsRoom.empty());
	CHECK(pending.CountsRoom == landed.CountsRoom);
	CHECK(pending.CountsRoom == thousands.CountsRoom);
	CHECK(pending.CountsRoom == "6 ahead, 99,999 behind");
}

TEST_CASE("Git window: a behind count wider than the usual room widens the reserved room to fit")
{
	Scene scene = MakeScene();

	const VsLabel huge = Describe(
		scene, "refs/heads/feature/x", Core::LineDistance{.Ahead = 6, .Behind = 1'234'567, .ForkTime = std::nullopt, .ForkOnLine = true});

	CHECK(huge.CountsRoom == "6 ahead, 1,234,567 behind");
}
