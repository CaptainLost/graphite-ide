#include <doctest/doctest.h>

#include "Git/Work/WorkBoard.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

	constexpr std::size_t kBudget = 100;

	const std::filesystem::path kMainPath = "C:/repo";

	Core::Oid OidOf(std::uint8_t seed)
	{
		Core::Oid id;
		id.Bytes[0] = seed;
		return id;
	}

	std::chrono::sys_seconds At(int seconds)
	{
		return std::chrono::sys_seconds{std::chrono::seconds{seconds}};
	}

	Core::PatchMatch Match(std::size_t ahead, std::size_t behind, std::size_t own, std::size_t inBase, bool complete = true)
	{
		return Core::PatchMatch{.Ahead = ahead, .Behind = behind, .OwnChanges = own, .InBase = inBase, .Complete = complete};
	}

	// main is the trunk through origin/main, feature is checked out in the main worktree.
	struct Scene
	{
		std::vector<Core::Branch> Branches;
		std::map<std::string, Core::PatchMatch> Matches;
		std::vector<std::string> Failed;
		std::vector<Graphite::WorktreeFacts> Linked;
		std::optional<std::size_t> WorkingCopyChanges = 0;
		std::filesystem::path WorkingDirectory;
		std::map<std::filesystem::path, std::string> Titles;
		bool HaveFacts = true;
		bool StaleFacts = false;
			std::string MovedSinceFacts; // full name of a branch that is at another tip now than when the facts were read
		Core::HeadState MainHead{.Kind = Core::HeadKind::Branch, .BranchName = "feature"};

		Scene()
		{
			Core::Branch remote;
			remote.Name = "origin/main";
			remote.FullName = "refs/remotes/origin/main";
			remote.Kind = Core::RefKind::Remote;
			remote.IsRemoteDefault = true;
			remote.Tip = OidOf(1);
			Branches.push_back(remote);

			Core::Branch trunk;
			trunk.Name = "main";
			trunk.FullName = "refs/heads/main";
			trunk.Tip = OidOf(1);
			trunk.Upstream = "origin/main";
			Branches.push_back(trunk);

			Core::Branch feature;
			feature.Name = "feature";
			feature.FullName = "refs/heads/feature";
			feature.Tip = OidOf(2);
			feature.IsHead = true;
			Branches.push_back(feature);
		}

		Core::Branch& Add(const std::string& name, int tipTime = 0, const std::string& subject = "")
		{
			Core::Branch branch;
			branch.Name = name;
			branch.FullName = Core::LocalBranchReference(name);
			branch.Tip = OidOf(static_cast<std::uint8_t>(10 + Branches.size()));
			Core::CommitSummary tip;
			tip.Summary = subject;
			tip.Committer.When = At(tipTime);
			branch.TipCommit = tip;
			Branches.push_back(branch);
			return Branches.back();
		}

		void Worktree(const std::string& branchName, std::optional<std::size_t> uncommitted, bool locked = false,
			const std::string& reason = "")
		{
			Graphite::WorktreeFacts worktree;
			worktree.Entry.Path = std::filesystem::path{"C:/wt"} / branchName;
			worktree.Entry.Name = branchName;
			worktree.Entry.Head = {.Kind = Core::HeadKind::Branch, .BranchName = branchName};
			worktree.Entry.Locked = locked;
			worktree.Entry.LockReason = reason;
			worktree.Uncommitted = uncommitted;
			Linked.push_back(worktree);
		}

		std::filesystem::path PathOf(const std::string& branchName) const
		{
			return std::filesystem::path{"C:/wt"} / branchName;
		}

		Graphite::WorkBoard Build() const
		{
			Graphite::WorkFacts facts;
			facts.Answers = Graphite::WorkFactsRequestFor(Branches, kBudget);

			if (StaleFacts)
			{
				facts.Answers.TrunkTip = OidOf(200);
			}

			Graphite::WorktreeFacts main;
			main.Entry.Path = kMainPath;
			main.Entry.IsMain = true;
			main.Entry.Head = MainHead;
			facts.Worktrees.push_back(main);

			for (const Graphite::WorktreeFacts& worktree : Linked)
			{
				facts.Worktrees.push_back(worktree);
			}

			for (const Graphite::BranchTip& tip : facts.Answers.Branches)
			{
				Graphite::BranchFacts branch;
				branch.FullName = tip.FullName;
				const auto found = Matches.find(tip.FullName);

				if (found != Matches.end())
				{
					branch.Match = found->second;
				}
				else if (std::ranges::find(Failed, tip.FullName) != Failed.end())
				{
					branch.Failure = Core::Error{.Message = "could not read"};
				}
				else
				{
					continue;
				}

				facts.Branches.push_back(branch);
			}

			std::vector<Core::Branch> current = Branches;

			for (Core::Branch& branch : current)
			{
				if (branch.FullName == MovedSinceFacts)
				{
					branch.Tip = OidOf(222);
				}
			}

			Graphite::WorkBoardInput input;
			input.Branches = &current;
			input.Facts = HaveFacts ? &facts : nullptr;
			input.WorkingCopyChanges = WorkingCopyChanges;
			input.WorkingDirectory = WorkingDirectory;
			input.TitleOf = [this](const std::filesystem::path& worktree) -> std::optional<std::string> {
				const auto found = Titles.find(worktree);
				return found != Titles.end() ? std::optional<std::string>{found->second} : std::nullopt;
			};
			return Graphite::BuildWorkBoard(input);
		}
	};

	const Graphite::WorkCard* CardFor(const Graphite::WorkBoard& board, const std::string& branch)
	{
		for (const Graphite::WorkCard& card : board.Cards)
		{
			if (card.Branch == Core::LocalBranchReference(branch))
			{
				return &card;
			}
		}

		return nullptr;
	}

	const Graphite::ReadyEntry* ReadyFor(const Graphite::WorkBoard& board, const std::string& branch)
	{
		for (const Graphite::ReadyEntry& entry : board.Ready)
		{
			if (entry.Branch == Core::LocalBranchReference(branch))
			{
				return &entry;
			}
		}

		return nullptr;
	}
}

TEST_CASE("The working copy's card comes first even with nothing else to show")
{
	Scene scene;
	scene.WorkingCopyChanges = 3;

	const Graphite::WorkBoard board = scene.Build();

	REQUIRE(board.Cards.size() == 1);
	CHECK(board.Cards[0].Kind == Graphite::WorkCardKind::WorkingCopy);
	CHECK(board.Cards[0].Branch == "refs/heads/feature");
	CHECK(board.Cards[0].Title == "feature");
	CHECK(board.Cards[0].Uncommitted == 3);
	CHECK(board.Cards[0].Worktree == kMainPath);
	CHECK(board.Ready.empty());
	CHECK(board.Trunk == "refs/heads/main");
	CHECK_FALSE(board.FactsPending);
}

TEST_CASE("A branch with a change the trunk lacks gets a card that could be fast-forwarded")
{
	Scene scene;
	scene.Add("side", 10, "Add a thing");
	scene.Matches["refs/heads/side"] = Match(2, 0, 2, 0);

	const Graphite::WorkBoard board = scene.Build();

	REQUIRE(board.Cards.size() == 2);
	const Graphite::WorkCard* card = CardFor(board, "side");
	REQUIRE(card != nullptr);
	CHECK(card->Kind == Graphite::WorkCardKind::Branch);
	CHECK(card->State == Graphite::WorkState::CanFastForward);
	CHECK(card->Ahead == 2);
	CHECK(card->DistanceKnown);
	CHECK(card->Checked);
	CHECK(card->OwnChanges == 2);
	CHECK(card->TipSubject == "Add a thing");
	CHECK(card->TipTime == At(10));
}

TEST_CASE("A branch also behind the trunk reads as behind")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(2, 4, 2, 0);

	const Graphite::WorkBoard board = scene.Build();

	const Graphite::WorkCard* card = CardFor(board, "side");
	REQUIRE(card != nullptr);
	CHECK(card->State == Graphite::WorkState::BehindTrunk);
	CHECK(card->Behind == 4);
}

TEST_CASE("A branch whose commits are all in the trunk is ready to remove as merged")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(0, 3, 0, 0);

	const Graphite::WorkBoard board = scene.Build();

	CHECK(CardFor(board, "side") == nullptr);
	const Graphite::ReadyEntry* entry = ReadyFor(board, "side");
	REQUIRE(entry != nullptr);
	CHECK(entry->Reason == Graphite::ReadyReason::Merged);
}

TEST_CASE("A branch whose every change has a twin in the trunk is ready to remove with the same change")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(3, 5, 3, 3);

	const Graphite::WorkBoard board = scene.Build();

	CHECK(CardFor(board, "side") == nullptr);
	const Graphite::ReadyEntry* entry = ReadyFor(board, "side");
	REQUIRE(entry != nullptr);
	CHECK(entry->Reason == Graphite::ReadyReason::SameChangeInTrunk);
}

TEST_CASE("A branch whose only commits ahead are merges is ready to remove as merged")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(2, 1, 0, 0);

	const Graphite::WorkBoard board = scene.Build();

	const Graphite::ReadyEntry* entry = ReadyFor(board, "side");
	REQUIRE(entry != nullptr);
	CHECK(entry->Reason == Graphite::ReadyReason::Merged);
}

TEST_CASE("A merged branch whose worktree has uncommitted files keeps a card")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(0, 3, 0, 0);
	scene.Worktree("side", 2);

	const Graphite::WorkBoard board = scene.Build();

	const Graphite::WorkCard* card = CardFor(board, "side");
	REQUIRE(card != nullptr);
	CHECK(card->Uncommitted == 2);
	CHECK(card->Worktree == scene.PathOf("side"));
	CHECK(ReadyFor(board, "side") == nullptr);
}

TEST_CASE("A card that took a session's title says so")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(0, 3, 0, 0);
	scene.Worktree("side", 2);

	const Graphite::WorkCard* withoutTitle = nullptr;
	const Graphite::WorkBoard plain = scene.Build();
	withoutTitle = CardFor(plain, "side");
	REQUIRE(withoutTitle != nullptr);
	CHECK_FALSE(withoutTitle->FromSession);
	CHECK_FALSE(plain.Cards.front().FromSession);

	scene.Titles[scene.PathOf("side")] = "side";
	const Graphite::WorkBoard titled = scene.Build();
	const Graphite::WorkCard* card = CardFor(titled, "side");
	REQUIRE(card != nullptr);
	CHECK(card->FromSession);
	CHECK(card->Title == "side");
}

TEST_CASE("A worktree whose uncommitted files could not be counted keeps its branch out of the ready group")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(0, 3, 0, 0);
	scene.Worktree("side", std::nullopt);

	const Graphite::WorkBoard board = scene.Build();

	const Graphite::WorkCard* card = CardFor(board, "side");
	REQUIRE(card != nullptr);
	CHECK_FALSE(card->Uncommitted.has_value());
	CHECK(ReadyFor(board, "side") == nullptr);
}

TEST_CASE("A branch whose patch check did not finish gets a card that says it was not checked")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(3, 5, 3, 3, false);

	const Graphite::WorkBoard board = scene.Build();

	const Graphite::WorkCard* card = CardFor(board, "side");
	REQUIRE(card != nullptr);
	CHECK_FALSE(card->Checked);
	CHECK(ReadyFor(board, "side") == nullptr);
}

TEST_CASE("Before any facts land every branch is an unchecked card and the board says facts are pending")
{
	Scene scene;
	scene.Add("one");
	scene.Add("two");
	scene.HaveFacts = false;

	const Graphite::WorkBoard board = scene.Build();

	CHECK(board.FactsPending);
	REQUIRE(board.Cards.size() == 3);
	REQUIRE(CardFor(board, "one") != nullptr);
	REQUIRE(CardFor(board, "two") != nullptr);
	CHECK_FALSE(CardFor(board, "one")->Checked);
	CHECK_FALSE(CardFor(board, "two")->Checked);
	CHECK(board.Ready.empty());
}

TEST_CASE("Facts that answer other branches are used but the board says they are pending")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(0, 3, 0, 0);
	scene.StaleFacts = true;

	const Graphite::WorkBoard board = scene.Build();

	CHECK(board.FactsPending);
	CHECK(ReadyFor(board, "side") != nullptr);
}

TEST_CASE("A branch that moved since the facts were read is unchecked, not ready")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(0, 3, 0, 0);
	scene.MovedSinceFacts = "refs/heads/side";

	const Graphite::WorkBoard board = scene.Build();

	CHECK(board.FactsPending);
	CHECK(ReadyFor(board, "side") == nullptr);
	const Graphite::WorkCard* card = CardFor(board, "side");
	REQUIRE(card != nullptr);
	CHECK_FALSE(card->Checked);
}

TEST_CASE("A failed branch read gets an unchecked card")
{
	Scene scene;
	scene.Add("side");
	scene.Failed.push_back("refs/heads/side");

	const Graphite::WorkBoard board = scene.Build();

	const Graphite::WorkCard* card = CardFor(board, "side");
	REQUIRE(card != nullptr);
	CHECK_FALSE(card->Checked);
	CHECK_FALSE(board.FactsPending);
}

TEST_CASE("A locked worktree whose work is all in the trunk is information, not ready")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(0, 3, 0, 0);
	scene.Worktree("side", 0, true, "claude session");

	const Graphite::WorkBoard board = scene.Build();

	CHECK(CardFor(board, "side") == nullptr);
	CHECK(ReadyFor(board, "side") == nullptr);
	REQUIRE(board.Info.size() == 1);
	CHECK(board.Info[0].Kind == Graphite::WorkInfoKind::Locked);
	CHECK(board.Info[0].Detail == "claude session");
	CHECK(board.Info[0].Branch == "refs/heads/side");
	CHECK(board.Info[0].Worktree == scene.PathOf("side"));
}

TEST_CASE("A locked worktree with work of its own is a card marked locked")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(2, 0, 2, 0);
	scene.Worktree("side", 0, true, "claude session");

	const Graphite::WorkBoard board = scene.Build();

	const Graphite::WorkCard* card = CardFor(board, "side");
	REQUIRE(card != nullptr);
	CHECK(card->Locked);
	CHECK(board.Info.empty());
}

TEST_CASE("A detached worktree with changes is a card without a branch")
{
	Scene scene;
	Graphite::WorktreeFacts worktree;
	worktree.Entry.Path = "C:/wt/scratch";
	worktree.Entry.Head = {.Kind = Core::HeadKind::Detached, .Commit = OidOf(77)};
	worktree.Uncommitted = 4;
	scene.Linked.push_back(worktree);

	const Graphite::WorkBoard board = scene.Build();

	REQUIRE(board.Cards.size() == 2);
	const Graphite::WorkCard& card = board.Cards[1];
	CHECK(card.Kind == Graphite::WorkCardKind::DetachedWorktree);
	CHECK(card.Branch.empty());
	CHECK(card.Title == "scratch");
	CHECK(card.DetachedAt == OidOf(77));
	CHECK(card.Uncommitted == 4);
	CHECK(card.Worktree == std::filesystem::path{"C:/wt/scratch"});
}

TEST_CASE("A detached worktree in the middle of a rebase is a card even without changes")
{
	Scene scene;
	Graphite::WorktreeFacts worktree;
	worktree.Entry.Path = "C:/wt/scratch";
	worktree.Entry.Head = {.Kind = Core::HeadKind::Detached, .Commit = OidOf(77), .Operation = Core::RepositoryOperation::Rebase};
	worktree.Uncommitted = 0;
	scene.Linked.push_back(worktree);

	const Graphite::WorkBoard board = scene.Build();

	REQUIRE(board.Cards.size() == 2);
	CHECK(board.Cards[1].Kind == Graphite::WorkCardKind::DetachedWorktree);
	CHECK(board.Cards[1].Operation == Core::RepositoryOperation::Rebase);
}

TEST_CASE("A clean detached worktree is information")
{
	Scene scene;
	Graphite::WorktreeFacts worktree;
	worktree.Entry.Path = "C:/wt/scratch";
	worktree.Entry.Head = {.Kind = Core::HeadKind::Detached, .Commit = OidOf(77)};
	worktree.Uncommitted = 0;
	scene.Linked.push_back(worktree);

	const Graphite::WorkBoard board = scene.Build();

	CHECK(board.Cards.size() == 1);
	REQUIRE(board.Info.size() == 1);
	CHECK(board.Info[0].Kind == Graphite::WorkInfoKind::DetachedClean);
	CHECK(board.Info[0].Worktree == std::filesystem::path{"C:/wt/scratch"});
}

TEST_CASE("A worktree whose folder is gone is information")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(0, 3, 0, 0);
	Graphite::WorktreeFacts worktree;
	worktree.Entry.Path = "C:/wt/gone";
	worktree.Entry.FolderMissing = true;
	scene.Linked.push_back(worktree);

	const Graphite::WorkBoard board = scene.Build();

	CHECK(board.Cards.size() == 1);
	REQUIRE(board.Info.size() == 1);
	CHECK(board.Info[0].Kind == Graphite::WorkInfoKind::FolderMissing);
	CHECK(board.Info[0].Worktree == std::filesystem::path{"C:/wt/gone"});
}

TEST_CASE("The trunk and HEAD's branch get no card of their own")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(2, 0, 2, 0);

	const Graphite::WorkBoard board = scene.Build();

	CHECK(CardFor(board, "main") == nullptr);
	CHECK(ReadyFor(board, "main") == nullptr);
	CHECK(ReadyFor(board, "feature") == nullptr);
	int featureCards = 0;

	for (const Graphite::WorkCard& card : board.Cards)
	{
		featureCards += card.Branch == "refs/heads/feature" ? 1 : 0;
	}

	CHECK(featureCards == 1);
	CHECK(board.Cards[0].Kind == Graphite::WorkCardKind::WorkingCopy);
}

TEST_CASE("Cards are ordered by their newest tip after the working copy")
{
	Scene scene;
	scene.Add("old", 10);
	scene.Add("newest", 30);
	scene.Add("middle", 20);
	scene.Matches["refs/heads/old"] = Match(1, 0, 1, 0);
	scene.Matches["refs/heads/newest"] = Match(1, 0, 1, 0);
	scene.Matches["refs/heads/middle"] = Match(1, 0, 1, 0);
	Graphite::WorktreeFacts worktree;
	worktree.Entry.Path = "C:/wt/scratch";
	worktree.Entry.Head = {.Kind = Core::HeadKind::Detached, .Commit = OidOf(77)};
	worktree.Uncommitted = 1;
	scene.Linked.push_back(worktree);

	const Graphite::WorkBoard board = scene.Build();

	REQUIRE(board.Cards.size() == 5);
	CHECK(board.Cards[0].Kind == Graphite::WorkCardKind::WorkingCopy);
	CHECK(board.Cards[1].Branch == "refs/heads/newest");
	CHECK(board.Cards[2].Branch == "refs/heads/middle");
	CHECK(board.Cards[3].Branch == "refs/heads/old");
	CHECK(board.Cards[4].Kind == Graphite::WorkCardKind::DetachedWorktree);
}

TEST_CASE("A card takes the title of the Claude session that ran in its worktree")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(1, 0, 1, 0);
	scene.Worktree("side", 0);
	scene.Titles[scene.PathOf("side")] = "Fix the login";

	const Graphite::WorkBoard board = scene.Build();

	const Graphite::WorkCard* card = CardFor(board, "side");
	REQUIRE(card != nullptr);
	CHECK(card->Title == "Fix the login");
}

TEST_CASE("Without a trunk only the working copy is shown")
{
	Scene scene;
	scene.Branches.clear();
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(1, 0, 1, 0);
	scene.MainHead = {.Kind = Core::HeadKind::Detached, .Commit = OidOf(9)};

	const Graphite::WorkBoard board = scene.Build();

	CHECK_FALSE(board.Trunk.has_value());
	REQUIRE(board.Cards.size() == 1);
	CHECK(board.Cards[0].Kind == Graphite::WorkCardKind::WorkingCopy);
	CHECK(board.Cards[0].Branch == "HEAD");
	CHECK(board.Ready.empty());
}

TEST_CASE("A card names a branch unless it is a detached working copy's or a detached worktree's")
{
	Scene scene;
	scene.Add("side");
	scene.Matches["refs/heads/side"] = Match(1, 0, 1, 0);
	Graphite::WorktreeFacts scratch;
	scratch.Entry.Path = "C:/wt/scratch";
	scratch.Entry.Head = {.Kind = Core::HeadKind::Detached, .Commit = OidOf(77)};
	scratch.Uncommitted = 1;
	scene.Linked.push_back(scratch);

	const Graphite::WorkBoard onBranch = scene.Build();

	REQUIRE(onBranch.Cards.size() == 3);
	CHECK(Graphite::CardNamesBranch(onBranch.Cards[0]));
	CHECK(Graphite::CardNamesBranch(onBranch.Cards[1]));
	CHECK_FALSE(Graphite::CardNamesBranch(onBranch.Cards[2]));

	for (Core::Branch& branch : scene.Branches)
	{
		branch.IsHead = false;
	}

	const Graphite::WorkBoard detached = scene.Build();

	REQUIRE(!detached.Cards.empty());
	CHECK(detached.Cards[0].Branch == "HEAD");
	CHECK_FALSE(Graphite::CardNamesBranch(detached.Cards[0]));
}

TEST_CASE("The ready summary counts merged, same change and locked")
{
	Scene scene;
	scene.Add("merged-one");
	scene.Add("merged-two");
	scene.Add("twin");
	scene.Matches["refs/heads/merged-one"] = Match(0, 1, 0, 0);
	scene.Matches["refs/heads/merged-two"] = Match(0, 1, 0, 0);
	scene.Matches["refs/heads/twin"] = Match(1, 1, 1, 1);

	CHECK(Graphite::ReadySummary(scene.Build()) == "2 merged into main, 1 with the same change already in main");

	scene.Add("locked");
	scene.Matches["refs/heads/locked"] = Match(0, 1, 0, 0);
	scene.Worktree("locked", 0, true, "claude session");

	CHECK(Graphite::ReadySummary(scene.Build()) ==
		"2 merged into main, 1 with the same change already in main · 1 locked by another session");
}

TEST_CASE("The facts request names the trunk and every other local branch but HEAD's")
{
	Scene scene;
	scene.Add("side");

	const Graphite::WorkFactsRequest request = Graphite::WorkFactsRequestFor(scene.Branches, 50);

	CHECK(request.TrunkFullName == "refs/heads/main");
	CHECK(request.TrunkTip == OidOf(1));
	CHECK(request.PatchBudget == 50);
	REQUIRE(request.Branches.size() == 1);
	CHECK(request.Branches[0].FullName == "refs/heads/side");

	const std::vector<Core::Branch> none;
	const Graphite::WorkFactsRequest empty = Graphite::WorkFactsRequestFor(none, 50);

	CHECK(empty.TrunkFullName.empty());
	CHECK(empty.TrunkTip.IsZero());
	CHECK(empty.Branches.empty());
}

TEST_CASE("Open in a linked worktree, the working copy card is that worktree's, with its own session's title")
{
	Scene scene;
	scene.MainHead = Core::HeadState{.Kind = Core::HeadKind::Branch, .BranchName = "main"};
	scene.Worktree("feature", 0);
	scene.WorkingDirectory = std::filesystem::path("C:/wt/feature/");
	scene.Titles[kMainPath] = "The main checkout's session";
	scene.Titles[scene.PathOf("feature")] = "The feature's session";

	const Graphite::WorkBoard board = scene.Build();

	REQUIRE(!board.Cards.empty());
	const Graphite::WorkCard& workingCopy = board.Cards.front();
	CHECK(workingCopy.Kind == Graphite::WorkCardKind::WorkingCopy);
	CHECK(workingCopy.Branch == "refs/heads/feature");
	CHECK(workingCopy.Worktree == scene.PathOf("feature"));
	CHECK(workingCopy.Title == "The feature's session");
	CHECK(workingCopy.FromSession);
	CHECK(board.Cards.size() == 1);
}

TEST_CASE("A branch checked out in the main worktree while a linked one is open gets that worktree on its card")
{
	Scene scene;
	scene.MainHead = Core::HeadState{.Kind = Core::HeadKind::Branch, .BranchName = "side"};
	scene.Add("side", 10);
	scene.Matches["refs/heads/side"] = Match(1, 0, 1, 0);
	scene.Worktree("feature", 0);
	scene.WorkingDirectory = scene.PathOf("feature");

	const Graphite::WorkBoard board = scene.Build();
	const Graphite::WorkCard* side = CardFor(board, "side");

	REQUIRE(side != nullptr);
	CHECK(side->Worktree == kMainPath);
}

TEST_CASE("The board counts every worktree of the repository once the facts land, and none before")
{
	Scene scene;
	scene.Add("merged");
	scene.Add("side");
	scene.Matches["refs/heads/merged"] = Match(0, 1, 0, 0);
	scene.Matches["refs/heads/side"] = Match(2, 0, 2, 0);
	scene.Worktree("merged", 0);
	scene.Worktree("side", 0);

	// The main worktree and the two linked ones, one of them ready to remove and so on no card.
	CHECK(scene.Build().Worktrees == std::optional<std::size_t>{3});

	scene.HaveFacts = false;
	CHECK_FALSE(scene.Build().Worktrees.has_value());
}
