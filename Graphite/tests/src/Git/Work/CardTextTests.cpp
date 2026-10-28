#include <doctest/doctest.h>

#include "Git/Work/CardText.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

	bool Has(const std::vector<std::string>& chips, const std::string& text)
	{
		return std::find(chips.begin(), chips.end(), text) != chips.end();
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
	const std::vector<std::string> chips = Graphite::CardChips(BranchCard(), "main");

	REQUIRE(!chips.empty());
	CHECK(chips.front() == "3 ahead \xC2\xB7 7 behind");
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

	const std::vector<std::string> chips = Graphite::CardChips(card, "main");

	CHECK(Has(chips, "5 changes"));
	CHECK(Has(chips, "\xE2\x86\x91" "2 to push"));
	CHECK_FALSE(Has(chips, "\xE2\x86\x93" "0 to pull"));
}

TEST_CASE("The working copy card counts one change in the singular")
{
	Graphite::WorkCard card;
	card.Kind = Graphite::WorkCardKind::WorkingCopy;
	card.Branch = "refs/heads/main";
	card.Uncommitted = 1;

	const std::vector<std::string> chips = Graphite::CardChips(card, "main");

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
	const std::vector<std::string> counted = Graphite::CardChips(card, "main");
	CHECK(Has(counted, "4 uncommitted"));
	CHECK_FALSE(Has(counted, "counting changes\xE2\x80\xA6"));

	card.Uncommitted = 0;
	for (const std::string& chip : Graphite::CardChips(card, "main"))
	{
		CHECK(chip.find("uncommitted") == std::string::npos);
	}
}

TEST_CASE("A card with twins in the trunk says how many are already there")
{
	Graphite::WorkCard card = BranchCard();
	card.OwnChanges = 5;
	card.InTrunk = 2;

	CHECK(Has(Graphite::CardChips(card, "main"), "2 of 5 already in main"));

	card.InTrunk = 0;
	for (const std::string& chip : Graphite::CardChips(card, "main"))
	{
		CHECK(chip.find("already in") == std::string::npos);
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
		for (const std::string& chip : Graphite::CardChips(card, "main"))
		{
			CHECK(chip.find("not checked") == std::string::npos);
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

	const std::vector<std::string> chips = Graphite::CardChips(card, "main");

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

TEST_CASE("Review progress reads as waiting until it is known")
{
	CHECK(Graphite::ReviewProgressText(std::nullopt) == "Reviewed \xE2\x80\xA6");
	CHECK(Graphite::ReviewProgressText(Graphite::ViewedCount{.Viewed = 3, .Total = 10}) == "Reviewed 3/10");
	CHECK(Graphite::ReviewProgressText(Graphite::ViewedCount{.Viewed = 0, .Total = 0}) == "Reviewed 0/0");
}
