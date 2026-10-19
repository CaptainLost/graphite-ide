#include <doctest/doctest.h>

#include "Git/GitStatusText.h"

#include <IconsLucide.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using Graphite::GitStatusInputs;
using Graphite::GitStatusSegment;
using Graphite::GitStatusText;

namespace {

	const std::string kBranch = ICON_LC_GIT_BRANCH " ";
	const std::string kUp = ICON_LC_ARROW_UP;
	const std::string kDown = ICON_LC_ARROW_DOWN;
	const std::string kChanges = "\xE2\x80\xA2 ";

	Core::HeadState HeadOnBranch(const std::string& name)
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Branch;
		head.BranchName = name;
		constexpr std::size_t leadingByteCount = 4;
		const std::array<std::uint8_t, leadingByteCount> leadingBytes{0x7d, 0x49, 0xd3, 0xd1};
		Core::Oid commit;

		for (std::size_t byteIndex = 0; byteIndex < leadingBytes.size(); ++byteIndex)
		{
			commit.Bytes[byteIndex] = leadingBytes[byteIndex];
		}

		head.Commit = commit;
		return head;
	}

	Core::Branch LocalBranch(const std::string& name, bool isHead)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.Kind = Core::RefKind::Local;
		branch.IsHead = isHead;
		return branch;
	}

	Core::Branch TrackingBranch(const std::string& name, std::size_t ahead, std::size_t behind)
	{
		constexpr bool isCheckedOut = true;
		Core::Branch branch = LocalBranch(name, isCheckedOut);
		branch.Upstream = "origin/" + name;
		branch.Ahead = ahead;
		branch.Behind = behind;
		branch.DistanceKnown = true;
		return branch;
	}

	Core::WorkingTreeStatus StatusWithChangeCount(std::size_t count)
	{
		Core::WorkingTreeStatus status;

		for (std::size_t index = 0; index < count; ++index)
		{
			Core::FileStatus entry;
			entry.Path = "file" + std::to_string(index) + ".txt";
			entry.WorkTree = Core::StatusFlags::Modified;
			status.Entries.push_back(entry);
		}

		return status;
	}
}

TEST_CASE("Nothing is shown with no folder open")
{
	CHECK_FALSE(GitStatusText({}).has_value());
}

TEST_CASE("A folder that is not a repository says so and offers nothing to click")
{
	const std::optional<GitStatusSegment> segment = GitStatusText({.FolderOpen = true, .MissingRepository = true});

	REQUIRE(segment.has_value());
	CHECK(segment->Branch == "No repository");
	CHECK_FALSE(segment->HasRepository);
	CHECK(segment->Distance.empty());
	CHECK(segment->Changes.empty());
}

TEST_CASE("Nothing is guessed before the head has arrived")
{
	CHECK_FALSE(GitStatusText({.FolderOpen = true}).has_value());
}

TEST_CASE("A branch with no upstream shows its name alone")
{
	const Core::HeadState head = HeadOnBranch("main");
	constexpr bool isCheckedOut = true;
	const std::vector<Core::Branch> branches{LocalBranch("main", isCheckedOut)};

	const std::optional<GitStatusSegment> segment =
		GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});

	REQUIRE(segment.has_value());
	CHECK(segment->Branch == kBranch + "main");
	CHECK(segment->HasRepository);
	CHECK(segment->Distance.empty());
	CHECK(segment->BranchTooltip == "main has no upstream branch");
}

TEST_CASE("A tracking branch shows only the sides it differs on")
{
	const Core::HeadState head = HeadOnBranch("main");

	SUBCASE("in sync")
	{
		const std::vector<Core::Branch> branches{TrackingBranch("main", 0, 0)};
		const std::optional<GitStatusSegment> segment = GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});
		REQUIRE(segment.has_value());
		CHECK(segment->Distance == ICON_LC_CHECK);
		CHECK(segment->DistanceTooltip == "Up to date with origin/main");
		CHECK(segment->BranchTooltip == "main tracks origin/main");
	}

	SUBCASE("ahead only")
	{
		const std::vector<Core::Branch> branches{TrackingBranch("main", 2, 0)};
		const std::optional<GitStatusSegment> segment = GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});
		REQUIRE(segment.has_value());
		CHECK(segment->Distance == kUp + "2");
		CHECK(segment->DistanceTooltip == "2 commits ahead of origin/main");
	}

	SUBCASE("behind only")
	{
		const std::vector<Core::Branch> branches{TrackingBranch("main", 0, 1)};
		const std::optional<GitStatusSegment> segment = GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});
		REQUIRE(segment.has_value());
		CHECK(segment->Distance == kDown + "1");
		CHECK(segment->DistanceTooltip == "1 commit behind origin/main");
	}

	SUBCASE("both")
	{
		const std::vector<Core::Branch> branches{TrackingBranch("main", 2, 1)};
		const std::optional<GitStatusSegment> segment = GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});
		REQUIRE(segment.has_value());
		CHECK(segment->Distance == kUp + "2 " + kDown + "1");
		CHECK(segment->DistanceTooltip == "2 commits ahead of origin/main and 1 commit behind it");
	}

	SUBCASE("distance not measured")
	{
		Core::Branch branch = TrackingBranch("main", 0, 0);
		branch.DistanceKnown = false;
		const std::vector<Core::Branch> branches{branch};
		const std::optional<GitStatusSegment> segment = GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});
		REQUIRE(segment.has_value());
		CHECK(segment->Distance.empty());
		CHECK(segment->DistanceTooltip.empty());
	}
}

TEST_CASE("Only the checked out local branch supplies the distance")
{
	const Core::HeadState head = HeadOnBranch("main");

	Core::Branch other = TrackingBranch("side", 5, 5);
	other.IsHead = false;

	Core::Branch remote = TrackingBranch("main", 9, 9);
	remote.Kind = Core::RefKind::Remote;

	constexpr bool isCheckedOut = true;
	const std::vector<Core::Branch> branches{other, remote, LocalBranch("main", isCheckedOut)};

	const std::optional<GitStatusSegment> segment = GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});

	REQUIRE(segment.has_value());
	CHECK(segment->Distance.empty());
}

TEST_CASE("A detached head shows its short hash")
{
	Core::HeadState head = HeadOnBranch("");
	head.Kind = Core::HeadKind::Detached;

	const std::optional<GitStatusSegment> segment = GitStatusText({.FolderOpen = true, .Head = &head});

	REQUIRE(segment.has_value());
	CHECK(segment->Branch == kBranch + "7d49d3d (detached)");
	CHECK(segment->BranchTooltip == "HEAD is detached at 7d49d3d100000000000000000000000000000000");
	CHECK(segment->Distance.empty());
}

TEST_CASE("A repository with no commits says so beside its branch")
{
	Core::HeadState head;
	head.Kind = Core::HeadKind::Unborn;
	head.BranchName = "master";

	const std::optional<GitStatusSegment> segment = GitStatusText({.FolderOpen = true, .Head = &head});

	REQUIRE(segment.has_value());
	CHECK(segment->Branch == kBranch + "master (no commits)");
	CHECK(segment->BranchTooltip == "No commits yet on master");
}

TEST_CASE("An operation left half done follows the branch, also on a detached head")
{
	Core::HeadState head = HeadOnBranch("main");

	head.Operation = Core::RepositoryOperation::Merge;
	CHECK(GitStatusText({.FolderOpen = true, .Head = &head})->Branch == kBranch + "main \xC2\xB7 MERGING");

	head.Operation = Core::RepositoryOperation::Rebase;
	CHECK(GitStatusText({.FolderOpen = true, .Head = &head})->Branch == kBranch + "main \xC2\xB7 REBASING");

	head.Operation = Core::RepositoryOperation::CherryPick;
	CHECK(GitStatusText({.FolderOpen = true, .Head = &head})->Branch == kBranch + "main \xC2\xB7 CHERRY-PICKING");

	head.Operation = Core::RepositoryOperation::Revert;
	CHECK(GitStatusText({.FolderOpen = true, .Head = &head})->Branch == kBranch + "main \xC2\xB7 REVERTING");

	head.Operation = Core::RepositoryOperation::Bisect;
	CHECK(GitStatusText({.FolderOpen = true, .Head = &head})->Branch == kBranch + "main \xC2\xB7 BISECTING");

	head.Operation = Core::RepositoryOperation::ApplyMailbox;
	CHECK(GitStatusText({.FolderOpen = true, .Head = &head})->Branch == kBranch + "main \xC2\xB7 APPLYING");

	head.Kind = Core::HeadKind::Detached;
	head.Operation = Core::RepositoryOperation::Rebase;
	CHECK(GitStatusText({.FolderOpen = true, .Head = &head})->Branch ==
		  kBranch + "7d49d3d (detached) \xC2\xB7 REBASING");
}

TEST_CASE("The change count is one per changed path, and absent when clean")
{
	const Core::HeadState head = HeadOnBranch("main");

	const Core::WorkingTreeStatus clean = StatusWithChangeCount(0);
	const std::optional<GitStatusSegment> noChanges = GitStatusText({.FolderOpen = true, .Head = &head, .Status = &clean});
	CHECK(noChanges->Changes.empty());
	CHECK(noChanges->ChangesTooltip.empty());

	const Core::WorkingTreeStatus one = StatusWithChangeCount(1);
	const std::optional<GitStatusSegment> singleChange = GitStatusText({.FolderOpen = true, .Head = &head, .Status = &one});
	CHECK(singleChange->Changes == kChanges + "1 change");
	CHECK(singleChange->ChangesTooltip == "1 uncommitted change. Click to show it in Changes.");

	const Core::WorkingTreeStatus three = StatusWithChangeCount(3);
	const std::optional<GitStatusSegment> severalChanges = GitStatusText({.FolderOpen = true, .Head = &head, .Status = &three});
	CHECK(severalChanges->Changes == kChanges + "3 changes");
	CHECK(severalChanges->ChangesTooltip == "3 uncommitted changes. Click to show them in Changes.");
}

TEST_CASE("The branch label is the status text's branch without its glyph")
{
	const Core::HeadState head = HeadOnBranch("main");

	CHECK(Graphite::GitBranchLabel({.FolderOpen = true, .Head = &head}) == "main");

	Core::HeadState detached;
	detached.Kind = Core::HeadKind::Detached;
	detached.Commit = HeadOnBranch("x").Commit;
	const std::optional<std::string> label = Graphite::GitBranchLabel({.FolderOpen = true, .Head = &detached});

	REQUIRE(label.has_value());
	CHECK(label->ends_with(" (detached)"));
	CHECK(*label == GitStatusText({.FolderOpen = true, .Head = &detached})->Branch.substr(kBranch.size()));
}

TEST_CASE("The branch label is absent with no folder or no repository")
{
	const Core::HeadState head = HeadOnBranch("main");

	CHECK_FALSE(Graphite::GitBranchLabel({.Head = &head}).has_value());
	CHECK_FALSE(Graphite::GitBranchLabel({.FolderOpen = true, .MissingRepository = true}).has_value());
	CHECK_FALSE(Graphite::GitBranchLabel({.FolderOpen = true}).has_value());
}
