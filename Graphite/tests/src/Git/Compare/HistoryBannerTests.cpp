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

TEST_CASE("Git window: a tag has no vs label and no banner, even beside a branch of the same short name that has both")
{
	Scene scene = MakeScene();
	scene.Line().State = Core::BranchState::MergedByHistory;

	const BranchLines lines(scene.Read);
	const VsInput branch{scene.Read, lines, "refs/heads/feature", std::nullopt};
	const VsInput tag{scene.Read, lines, "refs/tags/feature", std::nullopt};
	const VsInput tagOfBase{scene.Read, lines, "refs/tags/develop", std::nullopt};

	REQUIRE_FALSE(Graphite::DescribeVs(branch).Lead.empty());
	REQUIRE(HistoryBannerFor(branch).has_value());

	CHECK(Graphite::DescribeVs(tag).Lead.empty());
	CHECK_FALSE(HistoryBannerFor(tag).has_value());
	CHECK(Graphite::DescribeVs(tagOfBase).Lead.empty());
	CHECK_FALSE(HistoryBannerFor(tagOfBase).has_value());
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

TEST_CASE("Git window: the local copy of a base or of an old copy is bannered as what its row stands for")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddRemote("fork");

	const std::size_t origin = builder.AddRemoteBranch("origin", "main");
	builder.MakeBase(origin);
	const std::size_t copy = builder.AddRemoteBranch("fork", "main");
	const std::size_t originLocal = builder.AddLocal("main", Graphite::Fixtures::BranchSpec{.State = Core::BranchState::NoOwnCommits});
	builder.SameRow(originLocal, origin);
	builder.Compare(originLocal, origin);
	const std::size_t copyLocal = builder.AddLocal("fork-main", Graphite::Fixtures::BranchSpec{.State = Core::BranchState::NoOwnCommits});
	builder.SameRow(copyLocal, copy);
	builder.Compare(copyLocal, origin);

	Scene scene;
	scene.Read = builder.Build();
	scene.Read.Model.OldCopies.push_back(Core::OldCopy{.Ref = copy, .Of = origin});

	CHECK_FALSE(Banner(scene, "refs/heads/main").has_value());
	REQUIRE(Banner(scene, "refs/heads/fork-main").has_value());
	CHECK(Banner(scene, "refs/heads/fork-main")->starts_with("The default branch of fork, made when the fork was."));
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

TEST_CASE("Git window: an old copy the user picked a line for has no old copy banner")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddRemote("fork");

	const std::size_t origin = builder.AddRemoteBranch("origin", "main");
	builder.MakeBase(origin);
	const std::size_t copy = builder.AddRemoteBranch("fork", "main");
	builder.Compare(copy, origin);

	Scene scene;
	scene.Read = builder.Build();
	scene.Read.Model.OldCopies.push_back(Core::OldCopy{.Ref = copy, .Of = origin});
	scene.Line().ByAnswer = true;

	CHECK_FALSE(Banner(scene, "refs/remotes/fork/main").has_value());
}

namespace {

	std::optional<std::string> BannerWithContent(Scene& scene, const Core::ContentVerdict& verdict)
	{
		scene.Line().State = Core::BranchState::NotMerged;

		const BranchLines lines(scene.Read);

		return HistoryBannerFor(VsInput{scene.Read, lines, "refs/heads/feature", std::nullopt, verdict});
	}
}

TEST_CASE("Git window: the banner names a squash's landing commit")
{
	Scene scene = MakeScene();
	const Core::Oid landing = scene.Read.Refs[scene.Line().Ref].Tip;

	const std::optional<std::string> banner = BannerWithContent(
		scene, Core::ContentVerdict{.Outcome = Core::ContentOutcome::Merged, .Kind = Core::ContentKind::Squash, .Landing = landing, .Twins = {}});

	CHECK(banner == std::optional<std::string>{"Merged into develop by a squash in " + landing.ShortHex() +
											   ": its changes are in develop, its commits are not."});
}

TEST_CASE("Git window: the banner of a squash without a landing names none")
{
	Scene scene = MakeScene();

	const std::optional<std::string> banner = BannerWithContent(
		scene, Core::ContentVerdict{.Outcome = Core::ContentOutcome::Merged, .Kind = Core::ContentKind::Squash, .Landing = std::nullopt, .Twins = {}});

	CHECK(banner == std::optional<std::string>{"Merged into develop by a squash: its changes are in develop, its commits are not."});
}

TEST_CASE("Git window: the banner of a branch merged by a rebase says each commit has a copy")
{
	Scene scene = MakeScene();

	const std::optional<std::string> banner = BannerWithContent(
		scene, Core::ContentVerdict{.Outcome = Core::ContentOutcome::Merged, .Kind = Core::ContentKind::Rebase, .Landing = std::nullopt, .Twins = {}});

	CHECK(banner == std::optional<std::string>{"Merged into develop by a rebase: each of its commits has a copy in develop."});
}

TEST_CASE("Git window: a branch the check found not merged gets no banner")
{
	Scene scene = MakeScene();

	CHECK_FALSE(BannerWithContent(scene, Core::ContentVerdict{}).has_value());
}
