#include <doctest/doctest.h>

#include "Review/ReviewMenu.h"

#include <string>
#include <vector>

using Graphite::ReviewAction;
using Graphite::ReviewMenu;
using Graphite::ReviewMenuItem;
using Graphite::ReviewSessions;

namespace {

	const std::string kMain = "refs/heads/main";
	const std::string kSide = "refs/heads/side";
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
}

TEST_CASE("A branch without a kept session offers Review and a submenu of other bases")
{
	const std::vector<Core::Branch> branches = {LocalBranch("main", kCheckedOut), LocalBranch("side", kNotCheckedOut)};
	const ReviewSessions sessions;

	const ReviewMenu menu = Graphite::ReviewMenuFor(branches[1], branches, sessions);

	REQUIRE(menu.Items.size() == 1);
	CHECK(menu.Items[0].Label == "Review");
	CHECK(menu.Items[0].Action == ReviewAction::Open);
	CHECK(menu.Items[0].Branch == kSide);
	CHECK(menu.Items[0].Base == kMain);
	CHECK(menu.Submenu == "Review against");
	REQUIRE(menu.SubmenuItems.size() == 1);
	CHECK(menu.SubmenuItems[0].Label == "main");
	CHECK(menu.SubmenuItems[0].Action == ReviewAction::Start);
	CHECK(menu.SubmenuItems[0].Branch == kSide);
	CHECK(menu.SubmenuItems[0].Base == kMain);
}

TEST_CASE("A draft counts as no session in the review menu")
{
	const std::vector<Core::Branch> branches = {LocalBranch("main", kCheckedOut), LocalBranch("side", kNotCheckedOut)};
	ReviewSessions sessions;
	sessions.OpenDraft(kSide, kMain);

	const ReviewMenu menu = Graphite::ReviewMenuFor(branches[1], branches, sessions);

	REQUIRE(menu.Items.size() == 1);
	CHECK(menu.Items[0].Label == "Review");
	CHECK(menu.Items[0].Base == kMain);
	CHECK(menu.Submenu == "Review against");
}

TEST_CASE("The checked-out branch is offered a review against its upstream and nothing without one")
{
	Core::Branch main = LocalBranch("main", kCheckedOut);
	main.Upstream = "origin/main";
	const std::vector<Core::Branch> branches = {main, RemoteBranch("origin/main")};
	const ReviewSessions sessions;

	const ReviewMenu menu = Graphite::ReviewMenuFor(branches[0], branches, sessions);

	REQUIRE(menu.Items.size() == 1);
	CHECK(menu.Items[0].Base == "refs/remotes/origin/main");

	const std::vector<Core::Branch> withoutUpstream = {LocalBranch("main", kCheckedOut)};
	CHECK(Graphite::ReviewMenuFor(withoutUpstream[0], withoutUpstream, sessions).Items.empty());
}

TEST_CASE("A kept session offers Open review and End review")
{
	const std::vector<Core::Branch> branches = {LocalBranch("main", kCheckedOut), LocalBranch("side", kNotCheckedOut)};
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);

	const ReviewMenu menu = Graphite::ReviewMenuFor(branches[1], branches, sessions);

	CHECK(menu.Submenu.empty());
	CHECK(menu.SubmenuItems.empty());
	REQUIRE(menu.Items.size() == 2);
	CHECK(menu.Items[0].Label == "Open review");
	CHECK(menu.Items[0].Action == ReviewAction::Open);
	CHECK(menu.Items[1].Label == "End review\xE2\x80\xA6");
	CHECK(menu.Items[1].Action == ReviewAction::End);
	CHECK(menu.Items[1].Branch == kSide);
}

TEST_CASE("With HEAD detached the other local branches are offered as bases in a submenu")
{
	const std::vector<Core::Branch> branches = {LocalBranch("main", kNotCheckedOut), LocalBranch("side", kNotCheckedOut),
												LocalBranch("next", kNotCheckedOut), RemoteBranch("origin/main")};
	const ReviewSessions sessions;

	const ReviewMenu menu = Graphite::ReviewMenuFor(branches[1], branches, sessions);

	CHECK(menu.Items.empty());
	CHECK(menu.Submenu == "Review against");
	REQUIRE(menu.SubmenuItems.size() == 2);
	CHECK(menu.SubmenuItems[0].Label == "main");
	CHECK(menu.SubmenuItems[0].Base == kMain);
	CHECK(menu.SubmenuItems[1].Label == "next");
	CHECK(menu.SubmenuItems[1].Branch == kSide);
	CHECK(menu.SubmenuItems[1].Action == ReviewAction::Start);
}

TEST_CASE("With HEAD detached and no other local branch nothing is offered")
{
	const std::vector<Core::Branch> branches = {LocalBranch("side", kNotCheckedOut), RemoteBranch("origin/main")};
	const ReviewSessions sessions;

	const ReviewMenu menu = Graphite::ReviewMenuFor(branches[0], branches, sessions);

	CHECK(menu.Items.empty());
	CHECK(menu.SubmenuItems.empty());
}
