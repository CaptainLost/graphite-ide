#include <doctest/doctest.h>

#include "Git/Work/CardText.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

	bool Has(const std::vector<Graphite::Chip>& chips, const std::string& text)
	{
		return std::ranges::find(chips, text, &Graphite::Chip::Text) != chips.end();
	}

	std::optional<Graphite::ChipTone> ToneOf(const std::vector<Graphite::Chip>& chips, const std::string& text)
	{
		const auto found = std::ranges::find(chips, text, &Graphite::Chip::Text);
		return found != chips.end() ? std::optional<Graphite::ChipTone>{found->Tone} : std::nullopt;
	}

	Graphite::WorkCard BranchCard()
	{
		Graphite::WorkCard card;
		card.Kind = Graphite::WorkCardKind::Branch;
		card.Branch = "refs/heads/feature";
		card.Ahead = 3;
		card.Behind = 7;
		card.DistanceKnown = true;
		card.Checked = true;
		return card;
	}
}

TEST_CASE("A branch card says how far it is from the trunk")
{
	const std::vector<Graphite::Chip> chips = Graphite::CardChips(BranchCard(), "main");

	REQUIRE(!chips.empty());
	CHECK(chips.front().Text == "3 ahead \xC2\xB7 7 behind");
}

TEST_CASE("The working copy card says its changes and what is left to push")
{
	Graphite::WorkCard card;
	card.Kind = Graphite::WorkCardKind::WorkingCopy;
	card.Branch = "refs/heads/feature";
	card.Worktree = "C:/repo";
	card.Uncommitted = 5;
	card.Ahead = 2;
	card.Behind = 0;
	card.DistanceKnown = true;
	card.Checked = true;

	const std::vector<Graphite::Chip> chips = Graphite::CardChips(card, "main");

	CHECK(Has(chips, "5 changes"));
	CHECK(Has(chips, "\xE2\x86\x91" "2 to push"));
	CHECK_FALSE(Has(chips, "\xE2\x86\x93" "0 to pull"));
}

TEST_CASE("A card's chips are toned: green ahead only, amber behind, blue for uncommitted work and twins")
{
	Graphite::WorkCard card = BranchCard();
	card.Behind = 0;
	CHECK(ToneOf(Graphite::CardChips(card, "main"), "3 ahead \xC2\xB7 0 behind") == Graphite::ChipTone::Good);

	card.Behind = 7;
	card.Worktree = "C:/repo-feature";
	card.Uncommitted = 2;
	card.OwnChanges = 5;
	card.InTrunk = 1;
	card.Operation = Core::RepositoryOperation::Rebase;
	card.Checked = false;

	const std::vector<Graphite::Chip> chips = Graphite::CardChips(card, "main");
	CHECK(ToneOf(chips, "3 ahead \xC2\xB7 7 behind") == Graphite::ChipTone::Warning);
	CHECK(ToneOf(chips, "2 uncommitted") == Graphite::ChipTone::Info);
	CHECK(ToneOf(chips, "1 of 5 already in main") == Graphite::ChipTone::Info);
	CHECK(ToneOf(chips, "rebase in progress") == Graphite::ChipTone::Warning);
	CHECK(ToneOf(chips, "not checked against main") == Graphite::ChipTone::Plain);

	Graphite::WorkCard workingCopy;
	workingCopy.Kind = Graphite::WorkCardKind::WorkingCopy;
	workingCopy.Branch = "refs/heads/main";
	workingCopy.Uncommitted = 4;
	CHECK(ToneOf(Graphite::CardChips(workingCopy, "main"), "4 changes") == Graphite::ChipTone::Info);

	workingCopy.Uncommitted = 0;
	CHECK(ToneOf(Graphite::CardChips(workingCopy, "main"), "0 changes") == Graphite::ChipTone::Plain);
}

TEST_CASE("The working copy card counts one change in the singular")
{
	Graphite::WorkCard card;
	card.Kind = Graphite::WorkCardKind::WorkingCopy;
	card.Branch = "refs/heads/main";
	card.Uncommitted = 1;

	const std::vector<Graphite::Chip> chips = Graphite::CardChips(card, "main");

	CHECK(Has(chips, "1 change"));
	CHECK_FALSE(Has(chips, "1 changes"));
}

TEST_CASE("Uncounted uncommitted files read as waiting, not as zero")
{
	Graphite::WorkCard card = BranchCard();
	card.Worktree = "C:/repo-feature";
	card.Uncommitted = std::nullopt;

	CHECK(Has(Graphite::CardChips(card, "main"), "counting changes\xE2\x80\xA6"));

	card.Uncommitted = 4;
	const std::vector<Graphite::Chip> counted = Graphite::CardChips(card, "main");
	CHECK(Has(counted, "4 uncommitted"));
	CHECK_FALSE(Has(counted, "counting changes\xE2\x80\xA6"));

	card.Uncommitted = 0;
	for (const Graphite::Chip& chip : Graphite::CardChips(card, "main"))
	{
		CHECK(chip.Text.find("uncommitted") == std::string::npos);
	}
}

TEST_CASE("A card with twins in the trunk says how many are already there")
{
	Graphite::WorkCard card = BranchCard();
	card.OwnChanges = 5;
	card.InTrunk = 2;

	CHECK(Has(Graphite::CardChips(card, "main"), "2 of 5 already in main"));

	card.InTrunk = 0;
	for (const Graphite::Chip& chip : Graphite::CardChips(card, "main"))
	{
		CHECK(chip.Text.find("already in") == std::string::npos);
	}
}

TEST_CASE("An unchecked card says it was not checked against the trunk")
{
	Graphite::WorkCard card = BranchCard();
	card.Checked = false;

	CHECK(Has(Graphite::CardChips(card, "main"), "not checked against main"));
}

TEST_CASE("Only a branch card says it was not checked: the working copy and a detached worktree have no patch check")
{
	Graphite::WorkCard workingCopy;
	workingCopy.Kind = Graphite::WorkCardKind::WorkingCopy;
	workingCopy.Branch = "refs/heads/feature";
	workingCopy.Uncommitted = 0;

	Graphite::WorkCard detached;
	detached.Kind = Graphite::WorkCardKind::DetachedWorktree;
	detached.Worktree = "C:/repo-detached";
	detached.Uncommitted = 2;

	for (const Graphite::WorkCard& card : {workingCopy, detached})
	{
		for (const Graphite::Chip& chip : Graphite::CardChips(card, "main"))
		{
			CHECK(chip.Text.find("not checked") == std::string::npos);
		}
	}
}

TEST_CASE("A detached card names its commit and the operation in progress")
{
	Graphite::WorkCard card = BranchCard();
	card.Locked = true;
	Core::Oid id;
	id.Bytes[0] = 0xAB;
	id.Bytes[1] = 0xCD;
	card.DetachedAt = id;
	card.Operation = Core::RepositoryOperation::CherryPick;

	const std::vector<Graphite::Chip> chips = Graphite::CardChips(card, "main");

	CHECK(Has(chips, "locked"));
	CHECK(Has(chips, "detached at " + id.ShortHex()));
	CHECK(Has(chips, "cherry-pick in progress"));
}

TEST_CASE("A card's branch line names the worktree folder, also of a path that ends in a separator")
{
	Graphite::WorkCard card = BranchCard();
	card.Worktree = std::filesystem::path("D:/Repos/feature-folder");
	CHECK(Graphite::CardBranchLine(card) == "feature \xC2\xB7 feature-folder");

	Graphite::WorkCard workingCopy;
	workingCopy.Kind = Graphite::WorkCardKind::WorkingCopy;
	workingCopy.Branch = "refs/heads/main";
	workingCopy.Worktree = std::filesystem::path("D:/Temp/bare/");
	CHECK(Graphite::CardBranchLine(workingCopy) == "main \xC2\xB7 bare");

	Graphite::WorkCard detached;
	detached.Kind = Graphite::WorkCardKind::DetachedWorktree;
	detached.Worktree = std::filesystem::path("D:/Temp/wt");
	CHECK(Graphite::CardBranchLine(detached) == "wt");

	CHECK(Graphite::CardBranchLine(BranchCard()) == "feature");
}

TEST_CASE("The Work summary counts the worktrees, reads as waiting until they are counted, and says what is left to push")
{
	Graphite::WorkBoard board;

	Graphite::WorkCard workingCopy;
	workingCopy.Kind = Graphite::WorkCardKind::WorkingCopy;
	workingCopy.Branch = "refs/heads/main";
	board.Cards.push_back(workingCopy);

	CHECK(Graphite::WorkSummaryText(board) == "counting worktrees\xE2\x80\xA6");

	board.Worktrees = 1;
	CHECK(Graphite::WorkSummaryText(board) == "1 worktree");

	board.Worktrees = 33;
	board.Cards.front().Ahead = 13;
	board.Cards.front().DistanceKnown = true;
	CHECK(Graphite::WorkSummaryText(board) == "33 worktrees \xC2\xB7 main \xE2\x86\x91" "13 to push");
}

TEST_CASE("A ready row names the branch, its worktree folder, why it is ready and when it last moved")
{
	Graphite::WorkBoard board;
	board.Trunk = "refs/heads/main";

	const std::chrono::sys_seconds tip{std::chrono::seconds{1700000000}};
	board.Ready.push_back(Graphite::ReadyEntry{.Branch = "refs/heads/worktree-calm-fox",
											   .Worktree = std::filesystem::path("D:/Repos/IDE/.claude/worktrees/calm-fox"),
											   .Reason = Graphite::ReadyReason::SameChangeInTrunk,
											   .TipTime = tip});
	board.Ready.push_back(Graphite::ReadyEntry{.Branch = "refs/heads/old", .Reason = Graphite::ReadyReason::Merged});
	board.Info.push_back(Graphite::WorkInfo{.Kind = Graphite::WorkInfoKind::Locked,
											.Worktree = std::filesystem::path("D:/wt/busy"),
											.Branch = "refs/heads/busy",
											.Detail = "claude session"});
	board.Info.push_back(Graphite::WorkInfo{.Kind = Graphite::WorkInfoKind::DetachedClean,
											.Worktree = std::filesystem::path("D:/Temp/wt")});

	REQUIRE(Graphite::ReadyLineCount(board) == 4);

	const Graphite::ReadyLine twin = Graphite::ReadyLineAt(board, 0);
	CHECK(twin.Name == "worktree-calm-fox");
	CHECK(twin.Folder == "calm-fox");
	CHECK(twin.Reason == "same change already in main");
	CHECK(twin.TipTime == std::optional<std::chrono::sys_seconds>{tip});
	CHECK_FALSE(twin.Locked);

	const Graphite::ReadyLine merged = Graphite::ReadyLineAt(board, 1);
	CHECK(merged.Name == "old");
	CHECK(merged.Folder.empty());
	CHECK(merged.Reason == "merged into main");

	// The folder is named as the branch, so it is said once.
	const Graphite::ReadyLine locked = Graphite::ReadyLineAt(board, 2);
	CHECK(locked.Name == "busy");
	CHECK(locked.Folder.empty());
	CHECK(locked.Reason == "locked: claude session");
	CHECK(locked.Locked);

	const Graphite::ReadyLine detached = Graphite::ReadyLineAt(board, 3);
	CHECK(detached.Name == "wt");
	CHECK(detached.Folder.empty());
	CHECK(detached.Reason == "detached, nothing to keep");
}

TEST_CASE("Review progress reads as waiting until it is known")
{
	CHECK(Graphite::ReviewProgressText(std::nullopt) == "counting viewed files\xE2\x80\xA6");
	CHECK(Graphite::ReviewProgressText(Graphite::ViewedCount{.Viewed = 3, .Total = 10}) == "3 of 10 viewed");
	CHECK(Graphite::ReviewProgressText(Graphite::ViewedCount{.Viewed = 0, .Total = 0}) == "0 of 0 viewed");
}

TEST_CASE("A card's review count reads as waiting until it is known")
{
	CHECK(Graphite::ReviewCountText(std::nullopt) == "\xE2\x80\xA6");
	CHECK(Graphite::ReviewCountText(Graphite::ViewedCount{.Viewed = 3, .Total = 10}) == "3/10");
}
