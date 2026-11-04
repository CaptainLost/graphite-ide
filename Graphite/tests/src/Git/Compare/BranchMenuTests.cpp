#include <doctest/doctest.h>

#include "Git/Compare/BranchMenu.h"
#include "Git/Lines/LineReadFixtures.h"

using Graphite::BranchLines;
using Graphite::BranchMenu;
using Graphite::LineRead;
using Graphite::MenuFor;
using Graphite::Fixtures::LineReadBuilder;

namespace {

	struct Scene
	{
		LineRead Read;
	};

	Scene MakeScene()
	{
		LineReadBuilder builder;
		builder.AddRemote("origin");

		const std::size_t develop = builder.AddLocal("develop");
		const std::size_t feature = builder.AddLocal("feature/x");
		builder.MakeBase(develop);
		builder.Compare(feature, develop);

		return Scene{builder.Build()};
	}
}

TEST_CASE("Git window: a base's menu offers stop using as base")
{
	Scene scene = MakeScene();
	const BranchLines lines(scene.Read);

	const BranchMenu base = MenuFor(&scene.Read, &lines, "refs/heads/develop");

	CHECK(base.StopUsingAsBase);
	CHECK_FALSE(base.UseAsBase);
	CHECK(base.CompareWith);

	const BranchMenu branch = MenuFor(&scene.Read, &lines, "refs/heads/feature/x");

	CHECK(branch.UseAsBase);
	CHECK_FALSE(branch.StopUsingAsBase);
	CHECK(branch.CompareWith);
}

TEST_CASE("Git window: use as main line is offered only without a server default")
{
	Scene scene = MakeScene();
	const BranchLines lines(scene.Read);

	CHECK_FALSE(MenuFor(&scene.Read, &lines, "refs/heads/feature/x").UseAsMainLine);
	CHECK_FALSE(MenuFor(&scene.Read, &lines, "refs/heads/develop").UseAsMainLine);

	scene.Read.Model.Bases.front().Reason = Core::BaseReason::MainLineGuess;

	CHECK(MenuFor(&scene.Read, &lines, "refs/heads/feature/x").UseAsMainLine);
	CHECK(MenuFor(&scene.Read, &lines, "refs/heads/develop").UseAsMainLine);

	scene.Read.Model.Bases.front().Reason = Core::BaseReason::MainLineByUser;

	CHECK_FALSE(MenuFor(&scene.Read, &lines, "refs/heads/develop").UseAsMainLine);
	CHECK(MenuFor(&scene.Read, &lines, "refs/heads/feature/x").UseAsMainLine);
}

TEST_CASE("Git window: the branch menu offers nothing without a read or for an unknown name")
{
	Scene scene = MakeScene();
	const BranchLines lines(scene.Read);

	const BranchMenu none = MenuFor(nullptr, nullptr, "refs/heads/develop");
	const BranchMenu unknown = MenuFor(&scene.Read, &lines, "refs/heads/gone");

	CHECK_FALSE(none.UseAsBase);
	CHECK_FALSE(none.StopUsingAsBase);
	CHECK_FALSE(unknown.UseAsBase);
	CHECK_FALSE(unknown.UseAsMainLine);
	CHECK_FALSE(unknown.CompareWith);
}
