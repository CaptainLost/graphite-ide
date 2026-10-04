#include <doctest/doctest.h>

#include "Git/GitStatusText.h"

#include <IconsMaterialDesignIcons.h>

#include <string>
#include <vector>

using Graphite::GitStatusInputs;
using Graphite::GitStatusText;

namespace {

	const std::string kBranch = ICON_MDI_SOURCE_BRANCH " ";
	const std::string kUp = ICON_MDI_ARROW_UP;
	const std::string kDown = ICON_MDI_ARROW_DOWN;
	const std::string kChanges = ICON_MDI_CIRCLE_MEDIUM " ";

	Core::HeadState OnBranch(const std::string& name)
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Branch;
		head.BranchName = name;
		// Oid has no parser; its bytes are set directly. Hex: 7d49d3d1 then zeros.
		Core::Oid commit;
		commit.Bytes[0] = 0x7d;
		commit.Bytes[1] = 0x49;
		commit.Bytes[2] = 0xd3;
		commit.Bytes[3] = 0xd1;
		head.Commit = commit;
		return head;
	}

	Core::Branch Local(const std::string& name, bool isHead)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.Kind = Core::RefKind::Local;
		branch.IsHead = isHead;
		return branch;
	}

	Core::Branch Tracking(const std::string& name, std::size_t ahead, std::size_t behind)
	{
		Core::Branch branch = Local(name, true);
		branch.Upstream = "origin/" + name;
		branch.Ahead = ahead;
		branch.Behind = behind;
		branch.DistanceKnown = true;
		return branch;
	}

	Core::WorkingTreeStatus Changed(std::size_t count)
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
	const auto segment = GitStatusText({.FolderOpen = true, .MissingRepository = true});

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
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches{Local("main", true)};

	const auto segment =
		GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});

	REQUIRE(segment.has_value());
	CHECK(segment->Branch == kBranch + "main");
	CHECK(segment->HasRepository);
	CHECK(segment->Distance.empty());
	CHECK(segment->BranchTooltip == "main has no upstream branch");
}

TEST_CASE("A tracking branch shows only the sides it differs on")
{
	const Core::HeadState head = OnBranch("main");

	SUBCASE("in sync")
	{
		const std::vector<Core::Branch> branches{Tracking("main", 0, 0)};
		const auto segment = GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});
		REQUIRE(segment.has_value());
		CHECK(segment->Distance == ICON_MDI_CHECK);
		CHECK(segment->DistanceTooltip == "Up to date with origin/main");
		CHECK(segment->BranchTooltip == "main tracks origin/main");
	}

	SUBCASE("ahead only")
	{
		const std::vector<Core::Branch> branches{Tracking("main", 2, 0)};
		const auto segment = GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});
		REQUIRE(segment.has_value());
		CHECK(segment->Distance == kUp + "2");
		CHECK(segment->DistanceTooltip == "2 commits ahead of origin/main");
	}

	SUBCASE("behind only")
	{
		const std::vector<Core::Branch> branches{Tracking("main", 0, 1)};
		const auto segment = GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});
		REQUIRE(segment.has_value());
		CHECK(segment->Distance == kDown + "1");
		CHECK(segment->DistanceTooltip == "1 commit behind origin/main");
	}

	SUBCASE("both")
	{
		const std::vector<Core::Branch> branches{Tracking("main", 2, 1)};
		const auto segment = GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});
		REQUIRE(segment.has_value());
		CHECK(segment->Distance == kUp + "2 " + kDown + "1");
		CHECK(segment->DistanceTooltip == "2 commits ahead of origin/main and 1 commit behind it");
	}

	SUBCASE("distance not measured")
	{
		Core::Branch branch = Tracking("main", 0, 0);
		branch.DistanceKnown = false;
		const std::vector<Core::Branch> branches{branch};
		const auto segment = GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});
		REQUIRE(segment.has_value());
		CHECK(segment->Distance.empty());
		CHECK(segment->DistanceTooltip.empty());
	}
}

TEST_CASE("Only the checked out local branch supplies the distance")
{
	const Core::HeadState head = OnBranch("main");

	Core::Branch other = Tracking("side", 5, 5);
	other.IsHead = false;

	Core::Branch remote = Tracking("main", 9, 9);
	remote.Kind = Core::RefKind::Remote;

	const std::vector<Core::Branch> branches{other, remote, Local("main", true)};

	const auto segment = GitStatusText({.FolderOpen = true, .Head = &head, .Branches = &branches});

	REQUIRE(segment.has_value());
	CHECK(segment->Distance.empty());
}

TEST_CASE("A detached head shows its short hash")
{
	Core::HeadState head = OnBranch("");
	head.Kind = Core::HeadKind::Detached;

	const auto segment = GitStatusText({.FolderOpen = true, .Head = &head});

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

	const auto segment = GitStatusText({.FolderOpen = true, .Head = &head});

	REQUIRE(segment.has_value());
	CHECK(segment->Branch == kBranch + "master (no commits)");
	CHECK(segment->BranchTooltip == "No commits yet on master");
}

TEST_CASE("An operation left half done follows the branch")
{
	Core::HeadState head = OnBranch("main");

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

	// A rebase usually detaches HEAD; the operation is still the thing to say.
	head.Kind = Core::HeadKind::Detached;
	head.Operation = Core::RepositoryOperation::Rebase;
	CHECK(GitStatusText({.FolderOpen = true, .Head = &head})->Branch ==
		  kBranch + "7d49d3d (detached) \xC2\xB7 REBASING");
}

TEST_CASE("The change count is one per changed path, and absent when clean")
{
	const Core::HeadState head = OnBranch("main");

	const Core::WorkingTreeStatus clean = Changed(0);
	const auto none = GitStatusText({.FolderOpen = true, .Head = &head, .Status = &clean});
	CHECK(none->Changes.empty());
	CHECK(none->ChangesTooltip.empty());

	const Core::WorkingTreeStatus one = Changed(1);
	const auto single = GitStatusText({.FolderOpen = true, .Head = &head, .Status = &one});
	CHECK(single->Changes == kChanges + "1 change");
	CHECK(single->ChangesTooltip == "1 uncommitted change. Click to show it in Changes.");

	const Core::WorkingTreeStatus three = Changed(3);
	const auto several = GitStatusText({.FolderOpen = true, .Head = &head, .Status = &three});
	CHECK(several->Changes == kChanges + "3 changes");
	CHECK(several->ChangesTooltip == "3 uncommitted changes. Click to show them in Changes.");
}
