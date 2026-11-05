#include <doctest/doctest.h>

#include "Git/Window/GitWindowRestore.h"

#include <optional>
#include <string>
#include <vector>

namespace {

	using namespace Graphite;

	Core::Branch Local(const std::string& name)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;
		return branch;
	}

	Core::Branch Remote(const std::string& name)
	{
		Core::Branch branch;
		branch.Kind = Core::RefKind::Remote;
		branch.Name = name;
		branch.FullName = "refs/remotes/" + name;
		return branch;
	}

	const std::vector<Core::Branch> kBranches = {Local("main"), Local("topic"), Remote("origin/side")};
}

TEST_CASE("Git window: a restored ref that is gone shows head's history")
{
	CHECK_FALSE(RefToRestore(GitWindowState{.Ref = "refs/remotes/origin/gone"}, kBranches).has_value());
	CHECK_FALSE(RefToRestore(GitWindowState{.Ref = "refs/heads/topic"}, {}).has_value());
	CHECK_FALSE(RefToRestore(GitWindowState{}, kBranches).has_value());

	CHECK(RefToRestore(GitWindowState{.Ref = "refs/remotes/origin/side"}, kBranches) ==
		  std::optional<std::string>{"refs/remotes/origin/side"});
	CHECK(RefToRestore(GitWindowState{.Ref = "refs/heads/topic"}, kBranches) == std::optional<std::string>{"refs/heads/topic"});
}

TEST_CASE("Git window: a restored ref waits for the branches and nothing else does")
{
	const GitWindowState named{.Ref = "refs/heads/topic"};

	CHECK(RestoreWaitsForBranches(named, BranchListing{}));
	CHECK_FALSE(RestoreWaitsForBranches(named, BranchListing{.Listed = true}));
	CHECK_FALSE(RestoreWaitsForBranches(named, BranchListing{.Failed = true}));
	CHECK_FALSE(RestoreWaitsForBranches(GitWindowState{}, BranchListing{}));
}
