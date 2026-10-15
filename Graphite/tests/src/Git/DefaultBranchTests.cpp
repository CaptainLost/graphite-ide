#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "Git/DefaultBranch.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

	constexpr bool kCheckedOut = true;
	constexpr bool kNotCheckedOut = false;

	constexpr std::uint8_t kMainTipByte = 1;
	constexpr std::uint8_t kOriginMainTipByte = 2;
	constexpr std::uint8_t kFeatureTipByte = 3;

	Core::Branch LocalBranch(const std::string& name, bool isHead, std::uint8_t tipByte)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;
		branch.Kind = Core::RefKind::Local;
		branch.IsHead = isHead;
		branch.Tip = OidWithFirstByte(tipByte);
		return branch;
	}

	Core::Branch RemoteBranch(const std::string& name, std::uint8_t tipByte)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/remotes/" + name;
		branch.Kind = Core::RefKind::Remote;
		branch.Tip = OidWithFirstByte(tipByte);
		return branch;
	}

	Core::Branch AsRemoteDefault(Core::Branch branch)
	{
		branch.IsRemoteDefault = true;
		return branch;
	}

	Core::Branch Tracking(Core::Branch branch, const std::string& upstream)
	{
		branch.Upstream = upstream;
		return branch;
	}
}

TEST_CASE("The trunk is the default branch and its upstream")
{
	const std::vector<Core::Branch> branches = {
		Tracking(LocalBranch("main", kNotCheckedOut, kMainTipByte), "origin/main"),
		LocalBranch("feature", kCheckedOut, kFeatureTipByte),
		AsRemoteDefault(RemoteBranch("origin/main", kOriginMainTipByte)),
	};

	CHECK(Graphite::TrunkTips(branches) ==
		  std::vector<Core::Oid>{OidWithFirstByte(kMainTipByte), OidWithFirstByte(kOriginMainTipByte)});
}

TEST_CASE("Without origin HEAD the trunk is the checked-out branch and its upstream")
{
	const std::vector<Core::Branch> branches = {
		Tracking(LocalBranch("main", kCheckedOut, kMainTipByte), "origin/main"),
		RemoteBranch("origin/main", kOriginMainTipByte),
	};

	CHECK(Graphite::TrunkTips(branches) ==
		  std::vector<Core::Oid>{OidWithFirstByte(kMainTipByte), OidWithFirstByte(kOriginMainTipByte)});
}

TEST_CASE("With no local branch tracking the remote default the trunk is that remote branch alone")
{
	const std::vector<Core::Branch> branches = {
		LocalBranch("feature", kCheckedOut, kFeatureTipByte),
		AsRemoteDefault(RemoteBranch("origin/main", kOriginMainTipByte)),
	};

	CHECK(Graphite::TrunkTips(branches) == std::vector<Core::Oid>{OidWithFirstByte(kOriginMainTipByte)});
}

TEST_CASE("A detached HEAD without origin HEAD leaves no trunk")
{
	const std::vector<Core::Branch> branches = {
		LocalBranch("feature", kNotCheckedOut, kFeatureTipByte),
		RemoteBranch("origin/main", kOriginMainTipByte),
	};

	CHECK(Graphite::TrunkTips(branches).empty());
}

TEST_CASE("A trunk level with its upstream is listed once")
{
	const std::vector<Core::Branch> branches = {
		Tracking(LocalBranch("main", kCheckedOut, kMainTipByte), "origin/main"),
		AsRemoteDefault(RemoteBranch("origin/main", kMainTipByte)),
	};

	CHECK(Graphite::TrunkTips(branches) == std::vector<Core::Oid>{OidWithFirstByte(kMainTipByte)});
}
