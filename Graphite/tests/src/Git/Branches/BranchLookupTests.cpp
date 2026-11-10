#include <doctest/doctest.h>

#include "Git/Branches/BranchLookup.h"

#include <optional>
#include <string>
#include <vector>

namespace {

	Core::Branch BranchOf(const std::string& name, Core::RefKind kind, bool isHead = false)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = (kind == Core::RefKind::Local ? "refs/heads/" : "refs/remotes/") + name;
		branch.Kind = kind;
		branch.IsHead = isHead;
		return branch;
	}
}

TEST_CASE("Git window: a branch is found by its full name, local or remote, and a name nobody has finds nothing")
{
	const std::vector<Core::Branch> branches = {BranchOf("main", Core::RefKind::Local, true),
												BranchOf("origin/main", Core::RefKind::Remote)};

	const Core::Branch* local = Graphite::BranchWithFullName(branches, "refs/heads/main");
	const Core::Branch* remote = Graphite::BranchWithFullName(branches, "refs/remotes/origin/main");

	REQUIRE(local != nullptr);
	REQUIRE(remote != nullptr);
	CHECK(local->Kind == Core::RefKind::Local);
	CHECK(remote->Kind == Core::RefKind::Remote);
	CHECK(Graphite::BranchWithFullName(branches, "main") == nullptr);
}

TEST_CASE("Git window: a remote branch is found by its short name and the checked-out branch is the local one at HEAD")
{
	const std::vector<Core::Branch> branches = {BranchOf("main", Core::RefKind::Local, true),
												BranchOf("origin/main", Core::RefKind::Remote)};

	const Core::Branch* remote = Graphite::RemoteBranchNamed(branches, "origin/main");

	REQUIRE(remote != nullptr);
	CHECK(remote->FullName == "refs/remotes/origin/main");
	CHECK(Graphite::RemoteBranchNamed(branches, "main") == nullptr);
	REQUIRE(Graphite::CheckedOutBranch(branches) != nullptr);
	CHECK(Graphite::CheckedOutBranch(branches)->Name == "main");
	CHECK(Graphite::CheckedOutBranch({BranchOf("main", Core::RefKind::Local)}) == nullptr);
}

TEST_CASE("Git window: a walk's upstream is its branch's, the checked-out branch's for HEAD, and none for a branch without one")
{
	Core::Branch main = BranchOf("main", Core::RefKind::Local, true);
	main.Upstream = "origin/main";

	Core::Branch feature = BranchOf("feature", Core::RefKind::Local);
	feature.Upstream = "fork/feature";

	const std::vector<Core::Branch> branches = {main, feature, BranchOf("solo", Core::RefKind::Local),
												BranchOf("origin/main", Core::RefKind::Remote)};

	CHECK(Graphite::UpstreamOfWalk(branches, std::nullopt) == "origin/main");
	CHECK(Graphite::UpstreamOfWalk(branches, std::string{"refs/heads/feature"}) == "fork/feature");
	CHECK(Graphite::UpstreamOfWalk(branches, std::string{"refs/heads/solo"}).empty());
	CHECK(Graphite::UpstreamOfWalk(branches, std::string{"refs/heads/gone"}).empty());
	CHECK(Graphite::UpstreamOfWalk({BranchOf("main", Core::RefKind::Local)}, std::nullopt).empty());
}
