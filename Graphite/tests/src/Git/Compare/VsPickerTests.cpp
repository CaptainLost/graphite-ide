#include <doctest/doctest.h>

#include "Git/Compare/VsPicker.h"
#include "Git/Lines/LineReadFixtures.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>

using Graphite::BranchLines;
using Graphite::BuildVsPicker;
using Graphite::ChangeFor;
using Graphite::LineAnswerChange;
using Graphite::LineRead;
using Graphite::VsOption;
using Graphite::VsPicker;
using Graphite::VsPickerInput;
using Graphite::Fixtures::LineReadBuilder;

namespace {

	struct Scene
	{
		LineRead Read;
		std::size_t Develop = 0;
		std::size_t Release = 0;
		std::size_t Parent = 0;
		std::size_t Feature = 0;

		Core::BranchLine& FeatureLine() { return Read.Model.Branches.back(); }
	};

	// develop and release are bases; feature/x is compared with develop; feature/parent is a branch.
	Scene MakeScene()
	{
		LineReadBuilder builder;
		builder.AddRemote("origin");

		Scene scene;
		scene.Develop = builder.AddLocal("develop");
		builder.MakeBase(scene.Develop);
		scene.Release = builder.AddRemoteBranch("origin", "release/1.0");
		builder.MakeBase(scene.Release);
		scene.Parent = builder.AddLocal("feature/parent");
		builder.Compare(scene.Parent, scene.Develop);
		scene.Feature = builder.AddLocal("feature/x");
		builder.Compare(scene.Feature, scene.Develop);
		scene.Read = builder.Build();

		Core::BranchLine& line = scene.FeatureLine();
		line.Kind = Core::ComparedKind::Base;
		line.AutomaticKind = Core::ComparedKind::Base;
		line.AutomaticComparedWith = scene.Develop;

		return scene;
	}

	const std::function<std::optional<Core::LineDistance>(std::size_t)> kPendingDistance =
		[](std::size_t) { return std::optional<Core::LineDistance>{}; };

	const std::function<std::optional<Core::LineDistance>(const Core::OldCopy&)> kNoCopyDistance =
		[](const Core::OldCopy&) { return std::optional<Core::LineDistance>{}; };

	VsPicker Build(const Scene& scene, const std::string& branch, const std::string& search = {},
				   const std::function<std::optional<Core::LineDistance>(std::size_t)>& distance = kPendingDistance)
	{
		const BranchLines lines(scene.Read);

		return BuildVsPicker(VsPickerInput{.Read = scene.Read,
										   .Lines = lines,
										   .Branch = branch,
										   .Search = search,
										   .DistanceTo = distance,
										   .OldCopyDistance = kNoCopyDistance,
										   .Now = LineReadBuilder::Now()});
	}

	const VsOption* OptionOf(const VsPicker& picker, VsOption::Kind kind, const std::string& name = {})
	{
		for (const VsOption& option : picker.Options)
		{
			if (option.What == kind && (name.empty() || option.Name == name))
			{
				return &option;
			}
		}

		return nullptr;
	}
}

TEST_CASE("Git window: a stacked branch's picker offers the branch it is built on first")
{
	Scene scene = MakeScene();
	Core::BranchLine& line = scene.FeatureLine();
	line.Kind = Core::ComparedKind::Branch;
	line.AutomaticKind = Core::ComparedKind::Branch;
	line.AutomaticComparedWith = scene.Parent;
	line.ComparedWith = scene.Parent;

	const VsPicker picker = Build(scene, "refs/heads/feature/x");

	REQUIRE(picker.Options.size() >= 4);
	CHECK(picker.Title == "Compare feature/x with");
	CHECK(picker.Options[0].What == VsOption::Kind::BuiltOn);
	CHECK(picker.Options[0].Name == "feature/parent");
	CHECK(picker.Options[0].Why == "the branch it is built on");
	CHECK(picker.Options[0].Chosen);
	CHECK(picker.Options[1].Name == "develop");
	CHECK(picker.Options[2].Name == "origin/release/1.0");
	CHECK(picker.Options[3].What == VsOption::Kind::AnotherBranch);
	CHECK(picker.Options[3].Why == "search all 4");
	CHECK(picker.Options.back().What == VsOption::Kind::Nothing);
	CHECK(picker.Options.back().Why == "the branch on its own");
}

TEST_CASE("Git window: the automatic option is marked auto")
{
	Scene scene = MakeScene();

	const VsPicker picker = Build(scene, "refs/heads/feature/x");

	const VsOption* const develop = OptionOf(picker, VsOption::Kind::Base, "develop");
	const VsOption* const release = OptionOf(picker, VsOption::Kind::Base, "origin/release/1.0");
	const VsOption* const nothing = OptionOf(picker, VsOption::Kind::Nothing);

	REQUIRE(develop != nullptr);
	REQUIRE(release != nullptr);
	REQUIRE(nothing != nullptr);
	CHECK(develop->Automatic);
	CHECK(develop->Chosen);
	CHECK_FALSE(release->Automatic);
	CHECK_FALSE(release->Chosen);
	CHECK_FALSE(nothing->Automatic);
	CHECK(OptionOf(picker, VsOption::Kind::BuiltOn) == nullptr);
}

TEST_CASE("Git window: an option says how far it forked and how far apart the two are")
{
	Scene scene = MakeScene();
	const std::int64_t forked = std::chrono::duration_cast<std::chrono::seconds>(
									(LineReadBuilder::Now() - std::chrono::hours{24 * 3}).time_since_epoch())
									.count();

	const auto distance = [&](std::size_t ref) -> std::optional<Core::LineDistance> {
		if (ref == scene.Develop)
		{
			return Core::LineDistance{.Ahead = 6, .Behind = 3'448, .ForkTime = forked, .ForkOnLine = true};
		}

		if (ref == scene.Release)
		{
			return Core::LineDistance{.Ahead = 1, .Behind = 2, .ForkTime = forked, .ForkOnLine = false};
		}

		return std::nullopt;
	};

	const VsPicker picker = Build(scene, "refs/heads/feature/x", {}, distance);

	CHECK(OptionOf(picker, VsOption::Kind::Base, "develop")->Why == "forked 3d ago \xC2\xB7 on its line");
	CHECK(OptionOf(picker, VsOption::Kind::Base, "develop")->Counts == "6 ahead \xC2\xB7 3,448 behind");
	CHECK(OptionOf(picker, VsOption::Kind::Base, "origin/release/1.0")->Why == "forked 3d ago \xC2\xB7 via a merge");

	const VsPicker pending = Build(scene, "refs/heads/feature/x");
	CHECK(OptionOf(pending, VsOption::Kind::Base, "develop")->Why == "\xE2\x80\xA6");
	CHECK(OptionOf(pending, VsOption::Kind::Base, "develop")->Counts == "\xE2\x80\xA6");
}

TEST_CASE("Git window: a fork this moment reads forked now")
{
	Scene scene = MakeScene();
	const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(LineReadBuilder::Now().time_since_epoch()).count();

	const auto distance = [&](std::size_t ref) -> std::optional<Core::LineDistance> {
		if (ref == scene.Develop)
		{
			return Core::LineDistance{.Ahead = 0, .Behind = 0, .ForkTime = now, .ForkOnLine = true};
		}

		return Core::LineDistance{.Ahead = 0, .Behind = 0, .ForkTime = std::nullopt, .ForkOnLine = false};
	};

	const VsPicker picker = Build(scene, "refs/heads/feature/x", {}, distance);

	CHECK(OptionOf(picker, VsOption::Kind::Base, "develop")->Why == "forked now \xC2\xB7 on its line");
	CHECK(OptionOf(picker, VsOption::Kind::Base, "origin/release/1.0")->Why == "no commit in common");
}

TEST_CASE("Git window: choosing the automatic option clears the user's pick")
{
	Scene scene = MakeScene();
	scene.FeatureLine().ByAnswer = true;
	scene.FeatureLine().ComparedWith = scene.Release;

	const VsPicker picker = Build(scene, "refs/heads/feature/x");
	const VsOption* const develop = OptionOf(picker, VsOption::Kind::Base, "develop");
	const VsOption* const release = OptionOf(picker, VsOption::Kind::Base, "origin/release/1.0");

	REQUIRE(develop != nullptr);
	CHECK_FALSE(develop->Chosen);
	CHECK(release->Chosen);

	const std::optional<LineAnswerChange> automatic = ChangeFor(picker, *develop, "refs/heads/feature/x");
	REQUIRE(automatic.has_value());
	CHECK(automatic->What == LineAnswerChange::Kind::CompareAutomatically);
	CHECK(automatic->Ref == "refs/heads/feature/x");

	const std::optional<LineAnswerChange> pick = ChangeFor(picker, *release, "refs/heads/feature/x");
	REQUIRE(pick.has_value());
	CHECK(pick->What == LineAnswerChange::Kind::CompareWith);
	CHECK(pick->Target == "refs/remotes/origin/release/1.0");
}

TEST_CASE("Git window: choosing nothing compares with nothing")
{
	Scene scene = MakeScene();

	const VsPicker picker = Build(scene, "refs/heads/feature/x");
	const VsOption* const nothing = OptionOf(picker, VsOption::Kind::Nothing);
	const VsOption* const another = OptionOf(picker, VsOption::Kind::AnotherBranch);

	REQUIRE(nothing != nullptr);
	const std::optional<LineAnswerChange> change = ChangeFor(picker, *nothing, "refs/heads/feature/x");

	REQUIRE(change.has_value());
	CHECK(change->What == LineAnswerChange::Kind::CompareWithNothing);
	CHECK(change->Ref == "refs/heads/feature/x");
	CHECK_FALSE(ChangeFor(picker, *another, "refs/heads/feature/x").has_value());
}

TEST_CASE("Git window: nothing is automatic for a branch the rule compares with nothing")
{
	Scene scene = MakeScene();
	scene.FeatureLine().AutomaticComparedWith = std::nullopt;
	scene.FeatureLine().AutomaticKind = Core::ComparedKind::None;
	scene.FeatureLine().ComparedWith = std::nullopt;

	const VsPicker picker = Build(scene, "refs/heads/feature/x");
	const VsOption* const nothing = OptionOf(picker, VsOption::Kind::Nothing);

	CHECK(nothing->Automatic);
	CHECK(nothing->Chosen);
	CHECK(ChangeFor(picker, *nothing, "refs/heads/feature/x")->What == LineAnswerChange::Kind::CompareAutomatically);
}

TEST_CASE("Git window: another branch lists at most fifty matches")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");

	const std::size_t develop = builder.AddLocal("develop");
	builder.MakeBase(develop);
	const std::size_t self = builder.AddLocal("feature/self");
	builder.Compare(self, develop);

	for (int index = 0; index < 120; ++index)
	{
		builder.AddLocal("topic/" + std::to_string(index));
	}

	builder.AddLocal("unrelated");

	Scene scene;
	scene.Read = builder.Build();

	const VsPicker none = Build(scene, "refs/heads/feature/self");
	CHECK(OptionOf(none, VsOption::Kind::Branch) == nullptr);

	const VsPicker found = Build(scene, "refs/heads/feature/self", "TOPIC");
	const auto branches = std::count_if(found.Options.begin(), found.Options.end(),
										[](const VsOption& option) { return option.What == VsOption::Kind::Branch; });
	CHECK(branches == static_cast<std::ptrdiff_t>(Graphite::kMostOtherBranches));
	CHECK(OptionOf(found, VsOption::Kind::Nothing) == &found.Options.back());

	const VsPicker narrowed = Build(scene, "refs/heads/feature/self", "unrel");
	REQUIRE(OptionOf(narrowed, VsOption::Kind::Branch) != nullptr);
	CHECK(OptionOf(narrowed, VsOption::Kind::Branch)->Name == "unrelated");
	CHECK(ChangeFor(narrowed, *OptionOf(narrowed, VsOption::Kind::Branch), "refs/heads/feature/self")->Target == "refs/heads/unrelated");
}

TEST_CASE("Git window: old copies are listed under the bases")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddRemote("fork");

	const std::size_t develop = builder.AddLocal("develop");
	builder.MakeBase(develop);
	const std::size_t quiet = builder.AddRemoteBranch("origin", "legacy");
	builder.MakeBase(quiet, false);
	const std::size_t copy = builder.AddRemoteBranch("fork", "develop");
	const std::size_t feature = builder.AddLocal("feature");
	builder.Compare(feature, develop);

	Scene scene;
	scene.Read = builder.Build();
	scene.Read.Model.OldCopies.push_back(Core::OldCopy{.Ref = copy, .Of = develop});

	const auto behindBy = [](const Core::OldCopy&) -> std::optional<Core::LineDistance> {
		return Core::LineDistance{.Ahead = 0, .Behind = 12, .ForkTime = std::nullopt, .ForkOnLine = false};
	};

	const BranchLines lines(scene.Read);
	const VsPicker picker = BuildVsPicker(VsPickerInput{.Read = scene.Read,
														.Lines = lines,
														.Branch = "refs/heads/feature",
														.Search = {},
														.DistanceTo = kPendingDistance,
														.OldCopyDistance = behindBy,
														.Now = LineReadBuilder::Now()});

	REQUIRE(picker.Bases.size() == 2);
	CHECK(picker.Bases[0].Name == "develop");
	CHECK_FALSE(picker.Bases[0].OldCopy);
	CHECK(picker.Bases[1].Name == "fork/develop");
	CHECK(picker.Bases[1].OldCopy);
	CHECK(picker.Bases[1].Reason == "default branch on fork, but an old copy of develop (12 behind it), so it is never a line of its own");
	CHECK(picker.QuietBases == 1);
	CHECK(picker.Footer.starts_with("A branch is compared with the line it forked from:"));
}

TEST_CASE("Git window: a base's picker offers the other bases and no branch it is built on")
{
	Scene scene = MakeScene();

	const VsPicker picker = Build(scene, "refs/heads/develop");

	CHECK(picker.Title == "Compare develop with");
	CHECK(OptionOf(picker, VsOption::Kind::Base, "develop") == nullptr);
	REQUIRE(OptionOf(picker, VsOption::Kind::Base, "origin/release/1.0") != nullptr);
	CHECK_FALSE(OptionOf(picker, VsOption::Kind::Base, "origin/release/1.0")->Chosen);
}

TEST_CASE("Git window: a branch the read lacks has no picker")
{
	Scene scene = MakeScene();

	CHECK(Build(scene, "refs/heads/gone").Title.empty());
}
