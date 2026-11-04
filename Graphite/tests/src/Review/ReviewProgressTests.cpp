#include <doctest/doctest.h>

#include "Review/ReviewProgress.h"

#include <string>
#include <vector>

using Graphite::ReviewSessions;

namespace {

	const std::string kMain = "refs/heads/main";
	const std::string kSide = "refs/heads/side";
	const std::string kNext = "refs/heads/next";
	constexpr bool kCheckedOut = true;
	constexpr bool kNotCheckedOut = false;

	Core::Branch LocalBranch(const std::string& name, bool isHead)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;
		branch.Kind = Core::RefKind::Local;
		branch.IsHead = isHead;
		return branch;
	}

	Core::Branch RemoteBranch(const std::string& name)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/remotes/" + name;
		branch.Kind = Core::RefKind::Remote;
		return branch;
	}

	Core::Ref<const std::vector<Core::Branch>> MainAndSide()
	{
		return Core::CreateRef<const std::vector<Core::Branch>>(
			std::vector<Core::Branch>{LocalBranch("main", kCheckedOut), LocalBranch("side", kNotCheckedOut),
									  LocalBranch("next", kNotCheckedOut)});
	}
}

TEST_CASE("A branch with a session or a draft is reviewed against that session's base")
{
	const std::vector<Core::Branch> branches = *MainAndSide();
	ReviewSessions sessions;

	sessions.Start(kSide, kNext);
	CHECK(Graphite::ReviewBaseOf(kSide, branches, sessions) == kNext);

	sessions.OpenDraft(kNext, kSide);
	CHECK(Graphite::ReviewBaseOf(kNext, branches, sessions) == kSide);
}

TEST_CASE("A branch without a session is reviewed against the base its review is offered")
{
	Core::Branch main = LocalBranch("main", kNotCheckedOut);
	main.Upstream = "origin/main";
	Core::Branch originMain = RemoteBranch("origin/main");
	originMain.IsRemoteDefault = true;
	const std::vector<Core::Branch> branches = {main, LocalBranch("side", kCheckedOut), originMain};
	const ReviewSessions sessions;

	CHECK(Graphite::ReviewBaseOf(kSide, branches, sessions) == kMain);
	CHECK(Graphite::ReviewBaseOf(kMain, branches, sessions) == "refs/remotes/origin/main");
}

TEST_CASE("A review offered no base falls back to the trunk and has no base without a trunk")
{
	const std::vector<Core::Branch> branches = *MainAndSide();
	const ReviewSessions sessions;

	CHECK(Graphite::ReviewBaseOf("refs/heads/not-read-yet", branches, sessions) == kMain);

	const std::vector<Core::Branch> detached = {LocalBranch("main", kNotCheckedOut), LocalBranch("side", kNotCheckedOut)};
	CHECK_FALSE(Graphite::ReviewBaseOf(kSide, detached, sessions).has_value());
	CHECK(Graphite::NoReviewBaseMessage(kSide, detached) == "There is no trunk to review this branch against.");
}

TEST_CASE("The trunk is never reviewed against itself")
{
	const std::vector<Core::Branch> branches = *MainAndSide();
	const ReviewSessions sessions;

	CHECK_FALSE(Graphite::ReviewBaseOf(kMain, branches, sessions).has_value());
	CHECK(Graphite::NoReviewBaseMessage(kMain, branches) == "There is nothing to review main against.");
}
