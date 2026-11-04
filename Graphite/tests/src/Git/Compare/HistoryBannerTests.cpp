#include <doctest/doctest.h>

#include "Git/Compare/HistoryBanner.h"
#include "Git/Lines/LineReadFixtures.h"

#include <string>

using Graphite::BranchLines;
using Graphite::HistoryBannerFor;
using Graphite::LineRead;
using Graphite::VsInput;
using Graphite::Fixtures::LineReadBuilder;

namespace {

	struct Scene
	{
		LineRead Read;

		Core::BranchLine& Line() { return Read.Model.Branches.front(); }
	};

	Scene MakeScene()
	{
		LineReadBuilder builder;
		builder.AddRemote("origin");

		const std::size_t develop = builder.AddLocal("develop");
		builder.MakeBase(develop);
		const std::size_t feature = builder.AddLocal("feature");
		builder.Compare(feature, develop);

		Scene scene;
		scene.Read = builder.Build();
		scene.Line().Kind = Core::ComparedKind::Base;

		return scene;
	}

	std::optional<std::string> Banner(const Scene& scene, const std::string& ref,
									  std::optional<Core::LineDistance> distance = std::nullopt)
	{
		const BranchLines lines(scene.Read);

		return HistoryBannerFor(VsInput{scene.Read, lines, ref, distance});
	}
}

TEST_CASE("Git window: a merged branch gets the merged banner")
{
	Scene scene = MakeScene();
	scene.Line().State = Core::BranchState::MergedByHistory;

	CHECK(Banner(scene, "refs/heads/feature") ==
		  std::optional<std::string>{"Merged into develop: its commits are in develop's history. The newer commits of develop are dimmed."});
}

TEST_CASE("Git window: a branch with no own commits gets the banner naming where it points")
{
	Scene scene = MakeScene();
	scene.Line().State = Core::BranchState::NoOwnCommits;

	const std::string hash = scene.Read.Refs[scene.Line().Ref].Tip.ShortHex();

	CHECK(Banner(scene, "refs/heads/feature") ==
		  std::optional<std::string>{"No commits of its own yet: it points at " + hash +
									 " on develop. Shown, never hidden as merged, until it gets one."});
}

TEST_CASE("Git window: a branch still being worked on gets no banner")
{
	Scene scene = MakeScene();

	CHECK_FALSE(Banner(scene, "refs/heads/feature").has_value());
	CHECK_FALSE(Banner(scene, "refs/heads/develop").has_value());
}

TEST_CASE("Git window: an old copy of another remote's default gets the old copy banner")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddRemote("fork");

	const std::size_t origin = builder.AddRemoteBranch("origin", "main");
	builder.MakeBase(origin);
	const std::size_t copy = builder.AddRemoteBranch("fork", "main");

	Scene scene;
	scene.Read = builder.Build();
	scene.Read.Model.OldCopies.push_back(Core::OldCopy{.Ref = copy, .Of = origin});

	const std::optional<Core::LineDistance> behind =
		Core::LineDistance{.Ahead = 0, .Behind = 1'200, .ForkTime = std::nullopt, .ForkOnLine = true};

	CHECK(Banner(scene, "refs/remotes/fork/main", behind) ==
		  std::optional<std::string>{"The default branch of fork, made when the fork was. It is an old copy of origin/main, 1,200 "
									 "commits behind it, so no branch is compared with it."});

	CHECK(Banner(scene, "refs/remotes/fork/main")->find("\xE2\x80\xA6 commits behind it") != std::string::npos);
}

TEST_CASE("Git window: a history cut off and no common history each get a banner")
{
	Scene scene = MakeScene();

	scene.Line().State = Core::BranchState::HistoryCutOff;
	CHECK(Banner(scene, "refs/heads/feature") ==
		  std::optional<std::string>{"History cut off here: this clone is shallow, so the commit it shares with its line is not on this computer."});

	scene.Line().State = Core::BranchState::NoCommonHistory;
	CHECK(Banner(scene, "refs/heads/feature") == std::optional<std::string>{"No commit in common with any line."});
}
