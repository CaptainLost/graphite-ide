#include <doctest/doctest.h>

#include "Git/Worktrees/WorktreeListing.h"
#include "Git/Worktrees/WorktreesModel.h"

#include "DeferredJobs.h"
#include "WorktreeFixtures.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace Graphite;
using Graphite::Fixtures::FakeWorktreePort;
using Graphite::Fixtures::WorktreeOn;

namespace {

	constexpr std::int64_t kNow = 10'000'000;
	constexpr std::int64_t kDay = 24 * 3600;

	const std::filesystem::path kOpen = "D:/repo";

	std::chrono::sys_seconds At(std::int64_t secondsBeforeNow)
	{
		return std::chrono::sys_seconds{std::chrono::seconds{kNow - secondsBeforeNow}};
	}

	std::filesystem::path Folder(const std::string& name)
	{
		return "D:/wt/" + name;
	}

	Core::Ref<const std::vector<Core::Branch>> NewSnapshot()
	{
		return Core::CreateRef<const std::vector<Core::Branch>>();
	}

	// The open folder's main checkout and linked worktrees, each last active `secondsBeforeNow` ago.
	struct Repository
	{
		Repository()
		{
			Core::WorktreeEntry main = WorktreeOn("main", kOpen);
			main.IsMain = true;
			Port.Entries.push_back(main);
		}

		void AddWorktree(const std::string& name, std::int64_t secondsBeforeNow, std::size_t changes = 0)
		{
			Port.Entries.push_back(WorktreeOn("worktree-" + name, Folder(name)));
			ActiveAt(name, secondsBeforeNow);
			Port.Changes[Folder(name).generic_string()] = changes;
		}

		void ActiveAt(const std::string& name, std::int64_t secondsBeforeNow)
		{
			Port.Activity[Folder(name).generic_string()] = WorktreeActivity{.Reflog = At(secondsBeforeNow)};
		}

		// The list is read and every worktree counted once.
		void Settle(const std::filesystem::path& openFolder = kOpen)
		{
			Model.SetOnScreen(true);
			Model.Follow(NewSnapshot(), ++Listings, openFolder);
			Jobs.RunAll();
			Model.OnUpdate(Core::Timestep{0.0f});
			Jobs.RunAll();
			Model.OnUpdate(Core::Timestep{0.0f});
			REQUIRE(Model.IsFirstCountSettled());
		}

		// Every worktree in view is counted again.
		void CountAgain()
		{
			for (const Core::WorktreeEntry& entry : Port.Entries)
			{
				Model.DirtyInView(entry.Path);
			}

			Model.OnUpdate(Core::Timestep{WorktreesModel::kLongestPollSeconds});
			Jobs.RunAll();
			Model.OnUpdate(Core::Timestep{0.0f});
		}

		void ListAgain()
		{
			Model.Follow(NewSnapshot(), ++Listings, kOpen);
			Jobs.RunAll();
			Model.OnUpdate(Core::Timestep{0.0f});
			Jobs.RunAll();
			Model.OnUpdate(Core::Timestep{0.0f});
		}

		// The folder names of the shown rows, in order, "*" after the open folder's.
		std::vector<std::string> Shown(const WorktreeListing& listing) const
		{
			std::vector<std::string> names;

			for (const WorktreeListed& listed : listing.Shown)
			{
				names.push_back(Model.Entries()->at(listed.Entry).Path.filename().generic_string() + (listed.ThisFolder ? "*" : ""));
			}

			return names;
		}

		std::vector<std::string> Folded(const WorktreeListing& listing) const
		{
			std::vector<std::string> names;

			for (const std::size_t entry : listing.Folded)
			{
				names.push_back(Model.Entries()->at(entry).Path.filename().generic_string());
			}

			return names;
		}

		FakeWorktreePort Port;
		DeferredJobs Jobs;
		WorktreesModel Model{Port, Jobs.Submitter(), [] { return kNow; }};
		WorktreeOrder Order;
		std::uint64_t Listings = 0; // the branch listings that landed
	};

	using Names = std::vector<std::string>;
}

TEST_CASE("Worktrees section: the open folder comes first, the rest by last activity newest first, the name breaking a tie")
{
	Repository repository;
	repository.AddWorktree("oldest", 300);
	repository.AddWorktree("newest", 100);
	repository.AddWorktree("cherry", 200);
	repository.AddWorktree("banana", 200);
	repository.Settle();

	repository.Order.Follow(WorktreeOrderMoment::Opening, repository.Model, kOpen);

	CHECK(repository.Shown(repository.Order.Listing()) == Names{"repo*", "newest", "banana", "cherry", "oldest"});
	CHECK(repository.Order.Listing().Folded.empty());
	CHECK_FALSE(repository.Order.Listing().Waiting);
}

TEST_CASE("Worktrees section: a worktree idle for seven days with no changes folds")
{
	Repository repository;
	repository.AddWorktree("busy", 100);
	repository.AddWorktree("idle", 8 * kDay);
	repository.AddWorktree("almost", 7 * kDay - 60);
	repository.Settle();

	repository.Order.Follow(WorktreeOrderMoment::Opening, repository.Model, kOpen);

	CHECK(repository.Shown(repository.Order.Listing()) == Names{"repo*", "busy", "almost"});
	CHECK(repository.Folded(repository.Order.Listing()) == Names{"idle"});
	CHECK(repository.Order.Listing().FoldedWithChanges == 0);
}

TEST_CASE("Worktrees section: a worktree with changes never folds, however long idle")
{
	Repository repository;
	repository.AddWorktree("forgotten", 30 * kDay, 1);
	repository.Settle();

	repository.Order.Follow(WorktreeOrderMoment::Opening, repository.Model, kOpen);

	CHECK(repository.Shown(repository.Order.Listing()) == Names{"repo*", "forgotten"});
	CHECK(repository.Order.Listing().Folded.empty());
}

TEST_CASE("Worktrees section: a count landing between moments moves no row and is said in the fold row until the next moment")
{
	Repository repository;
	repository.AddWorktree("first", 100);
	repository.AddWorktree("second", 200);
	repository.AddWorktree("idle", 9 * kDay);
	repository.Settle();

	repository.Order.Follow(WorktreeOrderMoment::Opening, repository.Model, kOpen);
	const std::uint64_t built = repository.Order.Version();

	repository.ActiveAt("second", 5);
	repository.Port.Changes[Folder("idle").generic_string()] = 2;
	repository.CountAgain();
	REQUIRE(repository.Model.CountOf(Folder("idle")) == 2u);

	repository.Order.Follow(WorktreeOrderMoment::None, repository.Model, kOpen);

	CHECK(repository.Shown(repository.Order.Listing()) == Names{"repo*", "first", "second"});
	CHECK(repository.Folded(repository.Order.Listing()) == Names{"idle"});
	CHECK(repository.Order.Listing().FoldedWithChanges == 1);
	CHECK(repository.Order.Version() != built);

	repository.Order.Follow(WorktreeOrderMoment::SectionToggled, repository.Model, kOpen);

	CHECK(repository.Shown(repository.Order.Listing()) == Names{"repo*", "second", "first", "idle"});
	CHECK(repository.Order.Listing().Folded.empty());
	CHECK(repository.Order.Listing().FoldedWithChanges == 0);
}

TEST_CASE("Worktrees section: the listing is not rebuilt between moments, and is at a search and at an opening")
{
	Repository repository;
	repository.AddWorktree("first", 100);
	repository.AddWorktree("second", 200);
	repository.Settle();

	repository.Order.Follow(WorktreeOrderMoment::Opening, repository.Model, kOpen);
	const std::uint64_t built = repository.Order.Version();

	repository.ActiveAt("second", 5);
	repository.CountAgain();
	repository.Order.Follow(WorktreeOrderMoment::None, repository.Model, kOpen);

	CHECK(repository.Shown(repository.Order.Listing()) == Names{"repo*", "first", "second"});
	CHECK(repository.Order.Version() == built);

	repository.Order.Follow(WorktreeOrderMoment::SearchChanged, repository.Model, kOpen);
	CHECK(repository.Shown(repository.Order.Listing()) == Names{"repo*", "second", "first"});

	repository.ActiveAt("first", 1);
	repository.CountAgain();
	repository.Order.Follow(WorktreeOrderMoment::Opening, repository.Model, kOpen);
	CHECK(repository.Shown(repository.Order.Listing()) == Names{"repo*", "first", "second"});
}

TEST_CASE("Worktrees section: a worktree removed or added rebuilds the listing at once")
{
	Repository repository;
	repository.AddWorktree("first", 100);
	repository.AddWorktree("second", 200);
	repository.AddWorktree("third", 300);
	repository.Settle();

	repository.Order.Follow(WorktreeOrderMoment::Opening, repository.Model, kOpen);

	// "first" goes, and "third" was active since: the rebuild orders by what is known now.
	repository.Port.Entries.erase(repository.Port.Entries.begin() + 1);
	repository.ActiveAt("third", 10);
	repository.ListAgain();
	repository.CountAgain();
	repository.Order.Follow(WorktreeOrderMoment::None, repository.Model, kOpen);

	CHECK(repository.Shown(repository.Order.Listing()) == Names{"repo*", "third", "second"});

	repository.AddWorktree("fourth", 50);
	repository.ListAgain();
	repository.Order.Follow(WorktreeOrderMoment::None, repository.Model, kOpen);

	CHECK(repository.Shown(repository.Order.Listing()) == Names{"repo*", "third", "fourth", "second"});
}

TEST_CASE("Worktrees section: with a linked worktree open, its row is this folder and the main checkout is an ordinary row")
{
	Repository repository;
	repository.AddWorktree("linked", 500);
	repository.Port.Activity[kOpen.generic_string()] = WorktreeActivity{.Reflog = At(100)};
	repository.AddWorktree("other", 50);
	repository.Settle(Folder("linked"));

	repository.Order.Follow(WorktreeOrderMoment::Opening, repository.Model, Folder("linked"));

	CHECK(repository.Shown(repository.Order.Listing()) == Names{"linked*", "other", "repo"});
}

TEST_CASE("Worktrees section: a bare main repository has no row")
{
	Repository repository;
	repository.Port.Entries.clear();
	repository.AddWorktree("linked", 100);
	repository.AddWorktree("other", 50);
	repository.Settle(Folder("linked"));

	repository.Order.Follow(WorktreeOrderMoment::Opening, repository.Model, Folder("linked"));

	CHECK(repository.Shown(repository.Order.Listing()) == Names{"linked*", "other"});
}

TEST_CASE("Worktrees section: a repository without a linked worktree lists nothing")
{
	Repository repository;
	repository.Settle();

	repository.Order.Follow(WorktreeOrderMoment::Opening, repository.Model, kOpen);

	CHECK(repository.Order.Listing().Shown.empty());
	CHECK(repository.Order.Listing().Folded.empty());
	CHECK_FALSE(repository.Order.Listing().Waiting);
}

TEST_CASE("Worktrees section: the listing waits for the first count, then is built once it settles")
{
	Repository repository;
	repository.AddWorktree("first", 100);
	repository.AddWorktree("second", 200);
	repository.Model.SetOnScreen(true);

	repository.Order.Follow(WorktreeOrderMoment::Opening, repository.Model, kOpen);
	CHECK_FALSE(repository.Order.Listing().Waiting);
	CHECK(repository.Order.Listing().Shown.empty());

	repository.Model.Follow(NewSnapshot(), ++repository.Listings, kOpen);
	repository.Jobs.RunAll();
	repository.Model.OnUpdate(Core::Timestep{0.0f});
	repository.Order.Follow(WorktreeOrderMoment::None, repository.Model, kOpen);

	CHECK(repository.Order.Listing().Waiting);
	CHECK(repository.Order.Listing().Shown.empty());

	repository.Jobs.RunAll();
	repository.Model.OnUpdate(Core::Timestep{0.0f});
	repository.Order.Follow(WorktreeOrderMoment::None, repository.Model, kOpen);

	CHECK_FALSE(repository.Order.Listing().Waiting);
	CHECK(repository.Shown(repository.Order.Listing()) == Names{"repo*", "first", "second"});
}

TEST_CASE("Worktrees section: a worktree whose count has not landed by the cap is placed by its activity alone, and one with no activity read after every known one")
{
	Repository repository;
	repository.AddWorktree("known", 100);
	repository.AddWorktree("late", 50);
	repository.AddWorktree("unread", 10);

	std::vector<std::function<void()>> waiting;
	WorktreesModel model(repository.Port, [&waiting](std::function<void()> job) { waiting.push_back(std::move(job)); },
						 [] { return kNow; });
	model.SetOnScreen(true);
	model.Follow(NewSnapshot(), 1, kOpen);
	waiting.back()();
	waiting.clear();
	model.OnUpdate(Core::Timestep{0.0f});

	// The three activity reads, then the three counts.
	REQUIRE(waiting.size() == 6);

	// known is read whole; late's count is held past the cap, its activity not; unread is held whole.
	waiting[0]();
	waiting[1]();
	waiting[3]();
	model.OnUpdate(Core::Timestep{WorktreesModel::kFirstCountCapSeconds});
	REQUIRE(model.IsFirstCountSettled());
	REQUIRE_FALSE(model.CountOf(Folder("late")).has_value());

	WorktreeOrder order;
	order.Follow(WorktreeOrderMoment::Opening, model, kOpen);

	REQUIRE(order.Listing().Shown.size() == 4);
	CHECK(model.Entries()->at(order.Listing().Shown[1].Entry).Path == Folder("late"));
	CHECK(model.Entries()->at(order.Listing().Shown[2].Entry).Path == Folder("known"));
	CHECK(model.Entries()->at(order.Listing().Shown[3].Entry).Path == Folder("unread"));
}

TEST_CASE("Worktrees section: the section's moments are an opening, a search and the section opened or closed")
{
	WorktreeMoments moments;

	CHECK(moments.Next(1, "", true) == WorktreeOrderMoment::Opening);
	CHECK(moments.Next(1, "", true) == WorktreeOrderMoment::None);
	CHECK(moments.Next(1, "wt", true) == WorktreeOrderMoment::SearchChanged);
	CHECK(moments.Next(1, "wt", false) == WorktreeOrderMoment::SectionToggled);
	CHECK(moments.Next(2, "wt", false) == WorktreeOrderMoment::Opening);
	CHECK(moments.Next(2, "wt", false) == WorktreeOrderMoment::None);
}
