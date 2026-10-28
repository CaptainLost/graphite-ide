#include <doctest/doctest.h>

#include "Review/ReviewProgress.h"

#include "DeferredJobs.h"
#include "Review/FakeReviewPort.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using Graphite::RangeDiffKey;
using Graphite::RangeEnds;
using Graphite::ReviewKey;
using Graphite::ReviewModel;
using Graphite::ReviewProgress;
using Graphite::ReviewRange;
using Graphite::ReviewSessions;
using Graphite::ViewedCount;

namespace {

	const Core::Timestep kFrame{0.016f};

	const std::string kMain = "refs/heads/main";
	const std::string kSide = "refs/heads/side";
	const std::string kNext = "refs/heads/next";
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
		range.Ends = RangeEnds{.MergeBase = IdNumbered(mergeBase), .Tip = IdNumbered(tip)};
		return range;
	}

	Core::Diff ThreeFiles()
	{
		Core::Diff diff;
		diff.Files = {ModifiedFile("a.cpp", 1, 2), ModifiedFile("b.cpp", 3, 4), ModifiedFile("c.cpp", 5, 6)};
		return diff;
	}

	Core::Ref<const std::vector<Core::Branch>> MainAndSide()
	{
		return Core::CreateRef<const std::vector<Core::Branch>>(
			std::vector<Core::Branch>{LocalBranch("main", kCheckedOut), LocalBranch("side", kNotCheckedOut),
									  LocalBranch("next", kNotCheckedOut)});
	}

	void Settle(ReviewModel& model, DeferredJobs& jobs)
	{
		jobs.RunAll();
		model.OnUpdate(kFrame);
	}

	bool IsCount(const std::optional<ViewedCount>& count, std::size_t viewed, std::size_t total)
	{
		return count.has_value() && count->Viewed == viewed && count->Total == total;
	}
}

TEST_CASE("A branch with a session or a draft is reviewed against that session's base")
{
	const std::vector<Core::Branch> branches = *MainAndSide();
	ReviewSessions sessions;

	sessions.Start(kSide, kNext);
	CHECK(Graphite::ReviewBaseOf(kSide, branches, sessions) == kNext);

	sessions.OpenDraft(kNext, kSide);
	CHECK(Graphite::ReviewBaseOf(kNext, branches, sessions) == kSide);
}

TEST_CASE("A branch without a session is reviewed against the base its review is offered")
{
	Core::Branch main = LocalBranch("main", kNotCheckedOut);
	main.Upstream = "origin/main";
	Core::Branch originMain = RemoteBranch("origin/main");
	originMain.IsRemoteDefault = true;
	const std::vector<Core::Branch> branches = {main, LocalBranch("side", kCheckedOut), originMain};
	const ReviewSessions sessions;

	CHECK(Graphite::ReviewBaseOf(kSide, branches, sessions) == kMain);
	CHECK(Graphite::ReviewBaseOf(kMain, branches, sessions) == "refs/remotes/origin/main");
}

TEST_CASE("A review offered no base falls back to the trunk and has no base without a trunk")
{
	const std::vector<Core::Branch> branches = *MainAndSide();
	const ReviewSessions sessions;

	CHECK(Graphite::ReviewBaseOf("refs/heads/not-read-yet", branches, sessions) == kMain);

	const std::vector<Core::Branch> detached = {LocalBranch("main", kNotCheckedOut), LocalBranch("side", kNotCheckedOut)};
	CHECK_FALSE(Graphite::ReviewBaseOf(kSide, detached, sessions).has_value());
	CHECK(Graphite::NoReviewBaseMessage(kSide, detached) == "There is no trunk to review this branch against.");
}

TEST_CASE("The trunk is never reviewed against itself")
{
	const std::vector<Core::Branch> branches = *MainAndSide();
	const ReviewSessions sessions;

	CHECK_FALSE(Graphite::ReviewBaseOf(kMain, branches, sessions).has_value());
	CHECK(Graphite::NoReviewBaseMessage(kMain, branches) == "There is nothing to review main against.");
}

TEST_CASE("A kept session's progress counts its viewed files of the committed range")
{
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);
	const Core::Diff committed = ThreeFiles();
	sessions.Mark(kSide, Graphite::ReviewGroup::Commits, committed.Files[0]);
	sessions.Mark(kSide, Graphite::ReviewGroup::Uncommitted, committed.Files[1]);
	const ReviewRange range = RangeFrom(1, 2);

	CHECK(IsCount(Graphite::CountReviewProgress(sessions, kSide, &range, &committed), 1, 3));
}

TEST_CASE("Without a kept session no file of the committed range is viewed")
{
	ReviewSessions sessions;
	const Core::Diff committed = ThreeFiles();
	const ReviewRange range = RangeFrom(1, 2);

	CHECK(IsCount(Graphite::CountReviewProgress(sessions, kSide, &range, &committed), 0, 3));

	sessions.OpenDraft(kSide, kMain);
	CHECK(IsCount(Graphite::CountReviewProgress(sessions, kSide, &range, &committed), 0, 3));
}

TEST_CASE("Review progress is unknown while its range or diff is and for a range with a problem")
{
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);
	const Core::Diff committed = ThreeFiles();
	const ReviewRange range = RangeFrom(1, 2);
	ReviewRange missingBase;
	missingBase.Problem = Graphite::RangeProblem::BaseMissing;

	CHECK_FALSE(Graphite::CountReviewProgress(sessions, kSide, nullptr, nullptr).has_value());
	CHECK_FALSE(Graphite::CountReviewProgress(sessions, kSide, &range, nullptr).has_value());
	CHECK_FALSE(Graphite::CountReviewProgress(sessions, kSide, &missingBase, &committed).has_value());
	CHECK_FALSE(Graphite::CountReviewProgress(sessions, kNext, nullptr, nullptr).has_value());
}

TEST_CASE("A merged branch has no committed files to review with or without a session")
{
	ReviewSessions sessions;
	const ReviewRange merged = RangeFrom(7, 7);

	CHECK(IsCount(Graphite::CountReviewProgress(sessions, kSide, &merged, nullptr), 0, 0));

	sessions.Start(kSide, kMain);
	CHECK(IsCount(Graphite::CountReviewProgress(sessions, kSide, &merged, nullptr), 0, 0));
}

TEST_CASE("A branch's progress is unknown until its range and its diff are read and is then counted once")
{
	FakeReviewPort port;
	port.Range = RangeFrom(1, 2);
	port.RangeDiff = ThreeFiles();
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const ReviewSessions sessions;
	const Core::Ref<const std::vector<Core::Branch>> branches = MainAndSide();
	ReviewProgress progress;

	CHECK_FALSE(progress.CountOf(kSide, branches, sessions, model).has_value());
	Settle(model, jobs);
	CHECK_FALSE(progress.CountOf(kSide, branches, sessions, model).has_value());
	Settle(model, jobs);
	CHECK(IsCount(progress.CountOf(kSide, branches, sessions, model), 0, 3));
	CHECK(IsCount(progress.CountOf(kSide, branches, sessions, model), 0, 3));

	CHECK(port.RangeQueries == 1);
	CHECK(port.RangeDiffQueries == 1);
	CHECK(model.GetRange(ReviewKey{.Branch = kSide, .Base = kMain}) != nullptr);
}

TEST_CASE("A branch's progress stays unknown while its range cannot be read")
{
	FakeReviewPort port;
	port.RangeFailure = Core::Error{.Code = Core::ErrorCode::IoFailure, .Message = "the disk is gone"};
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const ReviewSessions sessions;
	ReviewProgress progress;

	CHECK_FALSE(progress.CountOf(kSide, MainAndSide(), sessions, model).has_value());
	Settle(model, jobs);
	CHECK_FALSE(progress.CountOf(kSide, MainAndSide(), sessions, model).has_value());
	CHECK(port.RangeDiffQueries == 0);
}

TEST_CASE("A branch's progress is unknown while the branches are unread and it has no session")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const ReviewSessions sessions;
	const Core::Ref<const std::vector<Core::Branch>> unread;
	ReviewProgress progress;

	CHECK_FALSE(progress.CountOf(kSide, unread, sessions, model).has_value());
	CHECK(jobs.Count() == 0);
}

TEST_CASE("A kept session's progress is counted against its own base with its own whitespace setting")
{
	FakeReviewPort port;
	port.Range = RangeFrom(1, 2);
	port.RangeDiff = ThreeFiles();
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	ReviewSessions sessions;
	sessions.Start(kSide, kNext);
	Graphite::SessionSettings settings = sessions.Find(kSide)->Settings;
	settings.Whitespace = Core::WhitespaceInDiff::Ignored;
	sessions.SetSettings(kSide, settings);
	const Core::Ref<const std::vector<Core::Branch>> branches = MainAndSide();
	ReviewProgress progress;

	static_cast<void>(progress.CountOf(kSide, branches, sessions, model));
	Settle(model, jobs);
	static_cast<void>(progress.CountOf(kSide, branches, sessions, model));
	Settle(model, jobs);

	CHECK(IsCount(progress.CountOf(kSide, branches, sessions, model), 0, 3));
	CHECK(model.GetRange(ReviewKey{.Branch = kSide, .Base = kNext}) != nullptr);
	CHECK(model.GetRange(ReviewKey{.Branch = kSide, .Base = kMain}) == nullptr);
	CHECK(port.LastRangeWhitespace == Core::WhitespaceInDiff::Ignored);
}

TEST_CASE("A kept session's progress follows a new mark")
{
	FakeReviewPort port;
	port.Range = RangeFrom(1, 2);
	port.RangeDiff = ThreeFiles();
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	ReviewSessions sessions;
	sessions.Start(kSide, kMain);
	const Core::Ref<const std::vector<Core::Branch>> branches = MainAndSide();
	ReviewProgress progress;

	static_cast<void>(progress.CountOf(kSide, branches, sessions, model));
	Settle(model, jobs);
	static_cast<void>(progress.CountOf(kSide, branches, sessions, model));
	Settle(model, jobs);
	REQUIRE(IsCount(progress.CountOf(kSide, branches, sessions, model), 0, 3));

	sessions.Mark(kSide, Graphite::ReviewGroup::Commits, port.RangeDiff.Files[2]);

	CHECK(IsCount(progress.CountOf(kSide, branches, sessions, model), 1, 3));
	CHECK(port.RangeDiffQueries == 1);
}

TEST_CASE("A draft's first mark makes its progress count that mark")
{
	FakeReviewPort port;
	port.Range = RangeFrom(1, 2);
	port.RangeDiff = ThreeFiles();
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	ReviewSessions sessions;
	sessions.OpenDraft(kSide, kMain);
	const Core::Ref<const std::vector<Core::Branch>> branches = MainAndSide();
	ReviewProgress progress;

	static_cast<void>(progress.CountOf(kSide, branches, sessions, model));
	Settle(model, jobs);
	static_cast<void>(progress.CountOf(kSide, branches, sessions, model));
	Settle(model, jobs);
	REQUIRE(IsCount(progress.CountOf(kSide, branches, sessions, model), 0, 3));

	sessions.Mark(kSide, Graphite::ReviewGroup::Commits, port.RangeDiff.Files[0]);

	CHECK(IsCount(progress.CountOf(kSide, branches, sessions, model), 1, 3));
}

TEST_CASE("A held count is not read again once its diff has left the model until its range changes")
{
	FakeReviewPort port;
	port.Range = RangeFrom(1, 2);
	port.RangeDiff = ThreeFiles();
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	ReviewSessions sessions;
	const Core::Ref<const std::vector<Core::Branch>> branches = MainAndSide();
	ReviewProgress progress;

	static_cast<void>(progress.CountOf(kSide, branches, sessions, model));
	Settle(model, jobs);
	static_cast<void>(progress.CountOf(kSide, branches, sessions, model));
	Settle(model, jobs);
	REQUIRE(IsCount(progress.CountOf(kSide, branches, sessions, model), 0, 3));

	constexpr std::uint8_t kFirstOtherTip = 10;

	for (std::size_t other = 0; other < ReviewModel::kCachedDiffs; ++other)
	{
		const auto tip = static_cast<std::uint8_t>(kFirstOtherTip + other);
		model.RequestCommittedDiff(RangeDiffKey{.Ends = RangeFrom(1, tip).Ends});
	}

	Settle(model, jobs);
	REQUIRE(model.GetCommittedDiff(RangeDiffKey{.Ends = RangeFrom(1, 2).Ends}) == nullptr);
	const int diffsRead = port.RangeDiffQueries;

	sessions.OpenDraft(kNext, kMain);
	CHECK(IsCount(progress.CountOf(kSide, branches, sessions, model), 0, 3));
	CHECK(port.RangeDiffQueries == diffsRead);

	port.Range = RangeFrom(1, 3);
	model.InvalidateRanges();
	static_cast<void>(progress.CountOf(kSide, branches, sessions, model));
	Settle(model, jobs);
	CHECK_FALSE(progress.CountOf(kSide, branches, sessions, model).has_value());
	Settle(model, jobs);
	CHECK(IsCount(progress.CountOf(kSide, branches, sessions, model), 0, 3));
	CHECK(port.RangeDiffQueries == diffsRead + 1);
}

TEST_CASE("Cleared progress counts again from the model")
{
	FakeReviewPort port;
	port.Range = RangeFrom(1, 2);
	port.RangeDiff = ThreeFiles();
	DeferredJobs jobs;
	ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const ReviewSessions sessions;
	const Core::Ref<const std::vector<Core::Branch>> branches = MainAndSide();
	ReviewProgress progress;

	static_cast<void>(progress.CountOf(kSide, branches, sessions, model));
	Settle(model, jobs);
	static_cast<void>(progress.CountOf(kSide, branches, sessions, model));
	Settle(model, jobs);
	REQUIRE(IsCount(progress.CountOf(kSide, branches, sessions, model), 0, 3));

	progress.Clear();
	model.Clear();

	CHECK_FALSE(progress.CountOf(kSide, branches, sessions, model).has_value());
}
