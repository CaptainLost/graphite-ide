#include <doctest/doctest.h>

#include "Review/ReviewMenu.h"

#include <IconsLucide.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using Graphite::ReviewAction;
using Graphite::ReviewMenu;
using Graphite::ReviewMenuItem;
using Graphite::ReviewRange;
using Graphite::ReviewSessions;

namespace {

	const std::string kMain = "refs/heads/main";
	const std::string kSide = "refs/heads/side";
	constexpr bool kCheckedOut = true;
	constexpr bool kNotCheckedOut = false;
	const std::string kNothingMore;
	const Core::Error* const kNoRangeError = nullptr;

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

	Core::Oid IdNumbered(std::uint8_t number)
	{
		Core::Oid id;
		id.Bytes[0] = number;
		return id;
	}

	Core::FileDiff ModifiedFile(const std::filesystem::path& path, std::uint8_t oldNumber, std::uint8_t newNumber)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.OldPath = path;
		file.NewPath = path;
		file.OldId = IdNumbered(oldNumber);
		file.NewId = IdNumbered(newNumber);
		return file;
	}

	ReviewRange RangeFrom(std::uint8_t mergeBase, std::uint8_t tip)
	{
		ReviewRange range;
		range.Ends = Graphite::RangeEnds{.MergeBase = IdNumbered(mergeBase), .Tip = IdNumbered(tip)};
		return range;
	}

	std::string Badge(std::string_view text)
	{
		return std::string{ICON_LC_CIRCLE_DOT} + std::string{text};
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

TEST_CASE("A branch without a session has no badge")
{
	const ReviewSessions sessions;
	const ReviewRange range = RangeFrom(1, 2);
	const Core::Diff committed;

	CHECK(Graphite::ReviewBadgeText(sessions, kSide, &range, kNoRangeError, &committed).empty());
}

TEST_CASE("A draft shows no badge")
{
	ReviewSessions sessions;
	sessions.OpenDraft(kSide, kMain);
	const ReviewRange range = RangeFrom(1, 2);
	const Core::Diff committed;

	CHECK(Graphite::ReviewBadgeText(sessions, kSide, &range, kNoRangeError, &committed).empty());
}

TEST_CASE("The badge counts the viewed files of the committed range")
{
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);
	Core::Diff committed;
	committed.Files = {ModifiedFile("a.cpp", 1, 2), ModifiedFile("b.cpp", 3, 4), ModifiedFile("c.cpp", 5, 6)};
	sessions.Mark(kSide, Graphite::ReviewGroup::Commits, committed.Files[0]);
	sessions.Mark(kSide, Graphite::ReviewGroup::Uncommitted, committed.Files[1]);
	const ReviewRange range = RangeFrom(1, 2);

	CHECK(Graphite::ReviewBadgeText(sessions, kSide, &range, kNoRangeError, &committed) == Badge(" 1/3"));
}

TEST_CASE("The badge says merged once the merge base is the tip")
{
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);
	const ReviewRange range = RangeFrom(7, 7);

	CHECK(Graphite::ReviewBadgeText(sessions, kSide, &range, kNoRangeError, nullptr) == std::string{ICON_LC_CHECK} + " merged");
}

TEST_CASE("The badge says nothing while loading and asks when its base is gone")
{
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);
	ReviewRange missingBase;
	missingBase.Problem = Graphite::RangeProblem::BaseMissing;
	const ReviewRange range = RangeFrom(1, 2);

	CHECK(Graphite::ReviewBadgeText(sessions, kSide, nullptr, kNoRangeError, nullptr) == Badge(kNothingMore));
	CHECK(Graphite::ReviewBadgeText(sessions, kSide, &range, kNoRangeError, nullptr) == Badge(kNothingMore));
	CHECK(Graphite::ReviewBadgeText(sessions, kSide, &missingBase, kNoRangeError, nullptr) == Badge(" ?"));
}

TEST_CASE("The badge asks once its range could not be read")
{
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);
	const Core::Error unreadable{.Code = Core::ErrorCode::IoFailure, .Message = "the disk is gone"};
	const Core::Ref<const ReviewRange> noRange;
	const Core::Ref<const Core::Diff> noDiff;
	Graphite::ReviewBadges badges;

	CHECK(Graphite::ReviewBadgeText(sessions, kSide, nullptr, &unreadable, nullptr) == Badge(" ?"));

	CHECK(badges.TextFor(sessions, kSide, noRange, kNoRangeError, noDiff) == Badge(kNothingMore));
	CHECK(badges.TextFor(sessions, kSide, noRange, &unreadable, noDiff) == Badge(" ?"));
}

TEST_CASE("A held badge answers without its diff for the same range and sessions but not after a mark or for a new range")
{
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);
	const Core::Ref<const ReviewRange> range = Core::CreateRef<const ReviewRange>(RangeFrom(1, 2));
	const Core::Ref<const ReviewRange> moved = Core::CreateRef<const ReviewRange>(RangeFrom(1, 3));
	const Core::Ref<const ReviewRange> merged = Core::CreateRef<const ReviewRange>(RangeFrom(7, 7));
	Core::Diff diff;
	diff.Files = {ModifiedFile("a.cpp", 1, 2)};
	const Core::Ref<const Core::Diff> committed = Core::CreateRef<const Core::Diff>(diff);
	const Core::Ref<const Core::Diff> diffOnItsWay;
	Graphite::ReviewBadges badges;

	CHECK_FALSE(badges.HeldFor(sessions, kSide, range).has_value());

	CHECK(badges.TextFor(sessions, kSide, range, kNoRangeError, diffOnItsWay) == Badge(kNothingMore));
	CHECK_FALSE(badges.HeldFor(sessions, kSide, range).has_value());

	CHECK(badges.TextFor(sessions, kSide, range, kNoRangeError, committed) == Badge(" 0/1"));
	CHECK(badges.HeldFor(sessions, kSide, range) == std::optional<std::string>{Badge(" 0/1")});
	CHECK_FALSE(badges.HeldFor(sessions, kSide, moved).has_value());

	sessions.Mark(kSide, Graphite::ReviewGroup::Commits, committed->Files[0]);
	CHECK_FALSE(badges.HeldFor(sessions, kSide, range).has_value());

	const std::string mergedText = std::string{ICON_LC_CHECK} + " merged";
	CHECK(badges.TextFor(sessions, kSide, merged, kNoRangeError, diffOnItsWay) == mergedText);
	CHECK(badges.HeldFor(sessions, kSide, merged) == std::optional<std::string>{mergedText});
}

TEST_CASE("A held badge is worked out again once a file is marked")
{
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);
	const Core::Ref<const ReviewRange> range = Core::CreateRef<const ReviewRange>(RangeFrom(1, 2));
	Core::Diff diff;
	diff.Files = {ModifiedFile("a.cpp", 1, 2), ModifiedFile("b.cpp", 3, 4)};
	const Core::Ref<const Core::Diff> committed = Core::CreateRef<const Core::Diff>(diff);
	Graphite::ReviewBadges badges;

	CHECK(badges.TextFor(sessions, kSide, range, kNoRangeError, committed) == Badge(" 0/2"));

	sessions.Mark(kSide, Graphite::ReviewGroup::Commits, committed->Files[1]);

	CHECK(badges.TextFor(sessions, kSide, range, kNoRangeError, committed) == Badge(" 1/2"));
}

TEST_CASE("A held badge is kept while its range, diff and sessions stay the same")
{
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);
	const Core::Ref<const ReviewRange> range = Core::CreateRef<const ReviewRange>(RangeFrom(1, 2));
	const Core::Ref<Core::Diff> diff = Core::CreateRef<Core::Diff>();
	diff->Files = {ModifiedFile("a.cpp", 1, 2)};
	const Core::Ref<const Core::Diff> committed = diff;
	Graphite::ReviewBadges badges;

	CHECK(badges.TextFor(sessions, kSide, range, kNoRangeError, committed) == Badge(" 0/1"));

	diff->Files.push_back(ModifiedFile("b.cpp", 3, 4));

	CHECK(badges.TextFor(sessions, kSide, range, kNoRangeError, committed) == Badge(" 0/1"));
}

TEST_CASE("A held badge is worked out again for a new range")
{
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);
	const Core::Ref<const ReviewRange> range = Core::CreateRef<const ReviewRange>(RangeFrom(1, 2));
	const Core::Ref<const ReviewRange> merged = Core::CreateRef<const ReviewRange>(RangeFrom(7, 7));
	Core::Diff diff;
	diff.Files = {ModifiedFile("a.cpp", 1, 2)};
	const Core::Ref<const Core::Diff> committed = Core::CreateRef<const Core::Diff>(diff);
	Graphite::ReviewBadges badges;

	CHECK(badges.TextFor(sessions, kSide, range, kNoRangeError, committed) == Badge(" 0/1"));
	CHECK(badges.TextFor(sessions, kSide, merged, kNoRangeError, committed) == std::string{ICON_LC_CHECK} + " merged");
}

TEST_CASE("A held badge is worked out again for a new diff")
{
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);
	const Core::Ref<const ReviewRange> range = Core::CreateRef<const ReviewRange>(RangeFrom(1, 2));
	Core::Diff oneFile;
	oneFile.Files = {ModifiedFile("a.cpp", 1, 2)};
	Core::Diff twoFiles;
	twoFiles.Files = {ModifiedFile("a.cpp", 1, 2), ModifiedFile("b.cpp", 3, 4)};
	const Core::Ref<const Core::Diff> first = Core::CreateRef<const Core::Diff>(oneFile);
	const Core::Ref<const Core::Diff> second = Core::CreateRef<const Core::Diff>(twoFiles);
	Graphite::ReviewBadges badges;

	CHECK(badges.TextFor(sessions, kSide, range, kNoRangeError, first) == Badge(" 0/1"));
	CHECK(badges.TextFor(sessions, kSide, range, kNoRangeError, second) == Badge(" 0/2"));
}
