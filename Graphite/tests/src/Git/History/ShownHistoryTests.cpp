#include <doctest/doctest.h>

#include "Git/History/ShownHistory.h"

#include "DeferredJobs.h"
#include "WorktreeFixtures.h"

#include "Git/Lines/LineReader.h"
#include "Git/Lines/HeldLines.h"
#include "Git/Worktrees/WorktreesModel.h"

#include "Core/Base/Base.h"
#include "Core/Git/Head.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace Graphite;

namespace {
	Core::Oid OidNumbered(std::uint8_t number)
	{
		Core::Oid id;
		id.Bytes[0] = number;
		return id;
	}

	// main is checked out and feature is built on it, two commits ahead; origin/main is the base both are compared with.
	Core::Ref<const LineRead> ReadOfMainAndFeature()
	{
		std::vector<Core::Branch> branches(3);
		branches[0].FullName = "refs/heads/main";
		branches[0].IsHead = true;
		branches[1].FullName = "refs/heads/feature";
		branches[2].FullName = "refs/remotes/origin/main";

		LineRead read;
		read.Refs.resize(3);
		read.Refs[0] = Core::LineRef{.FullName = "refs/heads/main", .ShortName = "main", .Tip = OidNumbered(1)};
		read.Refs[1] = Core::LineRef{.FullName = "refs/heads/feature", .ShortName = "feature", .Tip = OidNumbered(2)};
		read.Refs[2] = Core::LineRef{.FullName = "refs/remotes/origin/main", .Remote = "origin", .ShortName = "main", .Tip = OidNumbered(3)};
		read.Branches = Core::CreateRef<const std::vector<Core::Branch>>(std::move(branches));

		read.Model.Bases = {Core::LineBase{.Ref = 2, .Reason = Core::BaseReason::ServerDefault}};
		read.Model.Branches = {
			Core::BranchLine{.Ref = 0, .Kind = Core::ComparedKind::Base, .ComparedWith = 2, .State = Core::BranchState::MergedByHistory},
			Core::BranchLine{.Ref = 1, .Kind = Core::ComparedKind::Base, .ComparedWith = 2, .State = Core::BranchState::NotMerged, .Ahead = 2},
		};
		return Core::CreateRef<const LineRead>(std::move(read));
	}

	struct Window
	{
		HeldLines OnScreen;
		std::optional<std::string> SelectedRef;
		std::optional<Core::Oid> SelectedStash;
		std::optional<std::filesystem::path> SelectedWorktree;
		const WorktreesModel* Worktrees = nullptr;
		std::set<std::string> NewerShown;
		Core::HeadState Head{.Kind = Core::HeadKind::Branch, .BranchName = "main"};
		bool Reading = false;
		bool ReadFailed = false;
		bool FileHistory = false;
		bool RestoreWaits = false;

		ShownHistory Shown() const
		{
			return ShownHistory{ShownHistoryInput{.OnScreen = OnScreen,
												  .SelectedRef = SelectedRef,
												  .SelectedStash = SelectedStash,
												  .SelectedWorktree = SelectedWorktree,
												  .Worktrees = Worktrees,
												  .RestoreWaits = RestoreWaits,
												  .FileHistory = FileHistory,
												  .Head = &Head,
												  .Reading = Reading,
												  .ReadFailed = ReadFailed,
												  .NewerShown = NewerShown}};
		}
	};
}

TEST_CASE("Git window: HEAD's history names the branch the read on screen has checked out")
{
	Window window;
	window.OnScreen.Follow(ReadOfMainAndFeature());

	const ShownHistory shown = window.Shown();
	CHECK(shown.HeadPlacement() == HeadInRead::Agrees);
	CHECK(shown.RefName() == "refs/heads/main");
	CHECK(shown.IsHeadHistory());
	CHECK(shown.ShowsLocalChanges());
	CHECK_FALSE(shown.WaitsForLines());
	CHECK(shown.RefLine().Fold == LineFold::Combined);
}

TEST_CASE("Git window: after a checkout HEAD's history names nothing until a read names the new branch")
{
	Window window;
	window.OnScreen.Follow(ReadOfMainAndFeature());
	window.Head.BranchName = "feature";

	window.Reading = true;
	CHECK(window.Shown().HeadPlacement() == HeadInRead::WaitsForRead);
	CHECK(window.Shown().RefName().empty());
	CHECK(window.Shown().WaitsForLines());

	// With no read coming HEAD's history is shown alone.
	window.Reading = false;
	CHECK(window.Shown().HeadPlacement() == HeadInRead::Differs);
	CHECK(window.Shown().RefName().empty());
	CHECK_FALSE(window.Shown().WaitsForLines());
	CHECK(window.Shown().RefLine() == HistoryLine{});
}

TEST_CASE("Git window: a branch's history is compared with its line and folds the line's newer commits")
{
	Window window;
	window.OnScreen.Follow(ReadOfMainAndFeature());
	window.SelectedRef = "refs/heads/feature";

	const ShownHistory shown = window.Shown();
	CHECK(shown.RefName() == "refs/heads/feature");
	CHECK_FALSE(shown.IsHeadHistory());
	CHECK_FALSE(shown.ShowsLocalChanges());

	const HistoryLine line = shown.RefLine();
	CHECK(line.Fold == LineFold::Folded);
	CHECK(line.Line == "refs/remotes/origin/main");
	CHECK(line.BranchTip == OidNumbered(2));
	CHECK(line.LineTip == OidNumbered(3));

	const std::optional<DistanceTips> tips = shown.DistanceTipsOf("refs/heads/feature");
	REQUIRE(tips.has_value());
	CHECK(tips->From == OidNumbered(2));
	CHECK(tips->To == OidNumbered(3));

	REQUIRE(shown.ContentCandidate() != nullptr);
	CHECK(shown.ContentCandidate()->Ref == 1);

	// The base counts nothing and is no candidate for a check of its contents.
	CHECK_FALSE(shown.DistanceTipsOf("refs/remotes/origin/main").has_value());

	window.NewerShown.insert("refs/heads/feature");
	CHECK(window.Shown().RefLine().Fold == LineFold::Shown);

	// A file's history follows the file alone.
	window.FileHistory = true;
	CHECK(window.Shown().LineToShow() == HistoryLine{});
	CHECK(window.Shown().RefLine().Fold == LineFold::Shown);
	CHECK_FALSE(window.Shown().WaitsForLines());
}

TEST_CASE("Git window: the checked-out branch's history is HEAD's and has the Local changes row")
{
	Window window;
	window.OnScreen.Follow(ReadOfMainAndFeature());
	window.SelectedRef = "refs/heads/main";

	CHECK(window.Shown().IsHeadHistory());
	CHECK(window.Shown().ShowsLocalChanges());
}

TEST_CASE("Git window: a stash's, a tag's and a restored history waiting for its ref name no branch")
{
	Window window;
	window.OnScreen.Follow(ReadOfMainAndFeature());

	window.SelectedStash = OidNumbered(9);
	CHECK(window.Shown().RefName().empty());
	CHECK_FALSE(window.Shown().WaitsForLines());
	CHECK_FALSE(window.Shown().ShowsLocalChanges());
	CHECK_FALSE(window.Shown().IsHeadHistory());

	window.SelectedStash.reset();
	window.SelectedRef = "refs/tags/v1";
	CHECK(window.Shown().ShowsTag());
	CHECK(window.Shown().RefName().empty());
	CHECK_FALSE(window.Shown().WaitsForLines());

	window.SelectedRef.reset();
	window.RestoreWaits = true;
	CHECK(window.Shown().RefName().empty());
	CHECK_FALSE(window.Shown().IsHeadHistory());
	CHECK_FALSE(window.Shown().ShowsLocalChanges());
}

TEST_CASE("Git window: a branch's history waits for a first read, and for one that may hold a branch it lacks")
{
	Window window;
	window.SelectedRef = "refs/heads/feature";

	window.Reading = true;
	CHECK(window.Shown().WaitsForLines());
	CHECK(window.Shown().RefLine() == HistoryLine{});

	// A failure stays said until a read succeeds, so nothing waits for the one asked after it.
	window.ReadFailed = true;
	CHECK_FALSE(window.Shown().WaitsForLines());

	window.ReadFailed = false;
	window.OnScreen.Follow(ReadOfMainAndFeature());
	window.SelectedRef = "refs/heads/just-made";
	CHECK(window.Shown().WaitsForLines());

	window.Reading = false;
	CHECK_FALSE(window.Shown().WaitsForLines());
}

namespace {

	const std::filesystem::path kOpenFolder = "D:/repo";
	const std::filesystem::path kSideFolder = "D:/side";
	const std::filesystem::path kLooseFolder = "D:/loose";

	Core::WorktreeEntry DetachedWorktreeAt(std::filesystem::path path, std::uint8_t commit)
	{
		Core::WorktreeEntry entry;
		entry.Path = std::move(path);
		entry.Head.Kind = Core::HeadKind::Detached;
		entry.Head.Commit = OidNumbered(commit);
		return entry;
	}

	// The repository's worktrees as the worktrees model lists them for the open folder D:/repo.
	struct ListedWorktrees
	{
		Fixtures::FakeWorktreePort Port;
		DeferredJobs Jobs;
		WorktreesModel Model{Port, Jobs.Submitter(), [] { return std::int64_t{0}; }};

		void List(std::vector<Core::WorktreeEntry> entries)
		{
			Port.Entries = std::move(entries);
			Model.Follow(Core::CreateRef<const std::vector<Core::Branch>>(), kOpenFolder);
			Jobs.RunAll();
			Model.OnUpdate(Core::Timestep{0.0f});
		}
	};

	// main checked out in the open folder, feature in D:/side, and D:/loose detached at commit 7.
	std::vector<Core::WorktreeEntry> MainSideAndLoose()
	{
		return {Fixtures::WorktreeOn("main", kOpenFolder), Fixtures::WorktreeOn("feature", kSideFolder),
				DetachedWorktreeAt(kLooseFolder, 7)};
	}
}

TEST_CASE("Git window: a branch checked out in another worktree shows that worktree's changes row, and HEAD's keeps Local changes")
{
	ListedWorktrees worktrees;
	worktrees.List(MainSideAndLoose());

	Window window;
	window.OnScreen.Follow(ReadOfMainAndFeature());
	window.Worktrees = &worktrees.Model;

	window.SelectedRef = "refs/heads/feature";
	CHECK(window.Shown().ChangesWorktree() == kSideFolder);
	CHECK_FALSE(window.Shown().ShowsLocalChanges());
	CHECK(window.Shown().WorktreeShown() == nullptr);

	// The open folder's HEAD, by no ref or by its branch's, has Local changes as before and no other worktree's row.
	window.SelectedRef.reset();
	CHECK_FALSE(window.Shown().ChangesWorktree().has_value());
	CHECK(window.Shown().ShowsLocalChanges());

	window.SelectedRef = "refs/heads/main";
	CHECK_FALSE(window.Shown().ChangesWorktree().has_value());
	CHECK(window.Shown().ShowsLocalChanges());

	// A file's history is about that file, not about a worktree's changes.
	window.SelectedRef = "refs/heads/feature";
	window.FileHistory = true;
	CHECK_FALSE(window.Shown().ChangesWorktree().has_value());
}

TEST_CASE("Git window: a detached worktree selected shows the history from its HEAD, with that worktree's changes row")
{
	ListedWorktrees worktrees;
	worktrees.List(MainSideAndLoose());

	Window window;
	window.OnScreen.Follow(ReadOfMainAndFeature());
	window.Worktrees = &worktrees.Model;
	window.SelectedWorktree = kLooseFolder;

	const ShownHistory shown = window.Shown();
	REQUIRE(shown.WorktreeShown() != nullptr);
	CHECK(shown.WorktreeShown()->Path == kLooseFolder);
	CHECK(shown.WorktreeShown()->Head.Commit == OidNumbered(7));
	CHECK(shown.ChangesWorktree() == kLooseFolder);

	// It is neither HEAD's history nor a branch's: no Local changes row, no line, nothing to wait for.
	CHECK_FALSE(shown.IsHeadHistory());
	CHECK_FALSE(shown.ShowsLocalChanges());
	CHECK(shown.RefName().empty());
	CHECK(shown.RefLine() == HistoryLine{});
	CHECK_FALSE(shown.WaitsForLines());
}

TEST_CASE("Git window: a selected worktree that leaves the list falls back to the open folder's HEAD")
{
	ListedWorktrees worktrees;
	worktrees.List(MainSideAndLoose());

	Window window;
	window.OnScreen.Follow(ReadOfMainAndFeature());
	window.Worktrees = &worktrees.Model;
	window.SelectedWorktree = kLooseFolder;
	REQUIRE(window.Shown().WorktreeShown() != nullptr);

	worktrees.List({Fixtures::WorktreeOn("main", kOpenFolder), Fixtures::WorktreeOn("feature", kSideFolder)});

	CHECK(window.Shown().WorktreeShown() == nullptr);
	CHECK_FALSE(window.Shown().ChangesWorktree().has_value());
	CHECK(window.Shown().IsHeadHistory());
	CHECK(window.Shown().ShowsLocalChanges());
	CHECK(window.Shown().RefName() == "refs/heads/main");
}

TEST_CASE("Git window: a worktree whose folder is missing has no changes row, selected or holding the branch shown")
{
	std::vector<Core::WorktreeEntry> entries = MainSideAndLoose();
	entries[1].FolderMissing = true;
	entries[2].FolderMissing = true;

	ListedWorktrees worktrees;
	worktrees.List(entries);

	Window window;
	window.OnScreen.Follow(ReadOfMainAndFeature());
	window.Worktrees = &worktrees.Model;

	window.SelectedRef = "refs/heads/feature";
	CHECK_FALSE(window.Shown().ChangesWorktree().has_value());

	// A detached one cannot show its own history either, so the open folder's HEAD's is shown.
	window.SelectedRef.reset();
	window.SelectedWorktree = kLooseFolder;
	CHECK(window.Shown().WorktreeShown() == nullptr);
	CHECK_FALSE(window.Shown().ChangesWorktree().has_value());
	CHECK(window.Shown().IsHeadHistory());
}
