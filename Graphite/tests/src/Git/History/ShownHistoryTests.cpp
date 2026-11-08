#include <doctest/doctest.h>

#include "Git/History/ShownHistory.h"

#include "Git/Lines/LineReader.h"
#include "Git/Lines/LinesOnScreen.h"

#include "Core/Base/Base.h"
#include "Core/Git/Head.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"

#include <cstdint>
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
		LinesOnScreen OnScreen;
		std::optional<std::string> SelectedRef;
		std::optional<Core::Oid> SelectedStash;
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
