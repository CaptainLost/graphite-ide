#include <doctest/doctest.h>

#include "Review/ReviewWindows.h"

#include "DeferredJobs.h"
#include "Review/FakeReviewPort.h"

#include <string>
#include <vector>

using Graphite::BranchRename;
using Graphite::ReviewTabState;
using Graphite::ReviewTarget;
using Graphite::ReviewWindows;

namespace {

	Core::Branch BranchNamed(const std::string& fullName)
	{
		Core::Branch branch;
		branch.FullName = fullName;
		return branch;
	}

	const std::vector<Core::Branch> kMainAndSide = {BranchNamed("refs/heads/main"), BranchNamed("refs/heads/side")};
}

TEST_CASE("Opening a branch already under review raises its tab rather than adding one")
{
	ReviewWindows windows;

	windows.Open(ReviewTarget{.Branch = "refs/heads/side", .Base = "refs/heads/main"});
	windows.Open(ReviewTarget{.Branch = "refs/heads/side", .Base = "refs/heads/other"});

	CHECK(windows.GetCount() == 1);
	REQUIRE(windows.CaptureState().size() == 1);
	CHECK(windows.CaptureState().front().Base == std::optional<std::string>{"refs/heads/main"});
}

TEST_CASE("Open review tabs come back as they were captured")
{
	ReviewWindows before;
	before.RestoreState({ReviewTabState{.Branch = "refs/heads/side",
										.Base = "refs/heads/main",
										.IncludeUncommitted = true,
										.Selected = "src/a.cpp",
										.SelectedIsUncommitted = true}});

	ReviewWindows after;
	after.RestoreState(before.CaptureState());

	REQUIRE(after.CaptureState().size() == 1);
	CHECK(after.CaptureState() == before.CaptureState());
}

TEST_CASE("A restored selection among uncommitted files is dropped when the toggle was off")
{
	ReviewWindows windows;
	windows.RestoreState({ReviewTabState{.Branch = "refs/heads/side",
										 .Base = "refs/heads/main",
										 .IncludeUncommitted = false,
										 .Selected = "src/a.cpp",
										 .SelectedIsUncommitted = true}});

	CHECK(windows.CaptureState().front().Selected.empty());
}

TEST_CASE("A restored tab waits for the first branch snapshot and closes only when it lacks its branch")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	Graphite::ReviewModel model{port, jobs.Submitter(), NoWatches()};
	ReviewWindows windows;
	windows.RestoreState({ReviewTabState{.Branch = "refs/heads/side", .Base = "refs/heads/main"},
						  ReviewTabState{.Branch = "refs/heads/gone", .Base = "refs/heads/main"}});

	CHECK(windows.GetCount() == 2);

	static_cast<void>(windows.FollowBranches(kMainAndSide, model)); // no rename was noted

	CHECK(windows.IsOpen("refs/heads/side"));
	CHECK_FALSE(windows.IsOpen("refs/heads/gone"));
}

TEST_CASE("A tab the reader opened stays when its branch is missing so it can say so")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	Graphite::ReviewModel model{port, jobs.Submitter(), NoWatches()};
	ReviewWindows windows;
	windows.Open(ReviewTarget{.Branch = "refs/heads/gone", .Base = "refs/heads/main"});

	static_cast<void>(windows.FollowBranches(kMainAndSide, model)); // no rename was noted

	CHECK(windows.IsOpen("refs/heads/gone"));
}

TEST_CASE("A rename is followed once the snapshot shows it and is handed on for the marks")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	Graphite::ReviewModel model{port, jobs.Submitter(), NoWatches()};
	ReviewWindows windows;
	const Graphite::ReviewKey oldKey{.Branch = "refs/heads/before", .Base = "refs/heads/main"};
	model.RequestRange(oldKey);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetRange(oldKey) != nullptr);
	windows.Open(ReviewTarget{.Branch = oldKey.Branch, .Base = oldKey.Base});
	windows.NoteRename("refs/heads/before", "refs/heads/after");

	const std::vector<BranchRename> notYet =
		windows.FollowBranches({BranchNamed("refs/heads/main"), BranchNamed("refs/heads/before")}, model);
	CHECK(notYet.empty());
	CHECK(windows.IsOpen("refs/heads/before"));

	const std::vector<BranchRename> followed =
		windows.FollowBranches({BranchNamed("refs/heads/main"), BranchNamed("refs/heads/after")}, model);

	REQUIRE(followed.size() == 1);
	CHECK(followed.front().From == "refs/heads/before");
	CHECK(followed.front().To == "refs/heads/after");
	CHECK(windows.IsOpen("refs/heads/after"));
	CHECK_FALSE(windows.IsOpen("refs/heads/before"));
	CHECK(model.GetRange(oldKey) == nullptr);
}

TEST_CASE("A rename onto a branch with a tab of its own closes the old tab instead of doubling the identity")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	Graphite::ReviewModel model{port, jobs.Submitter(), NoWatches()};
	ReviewWindows windows;
	windows.Open(ReviewTarget{.Branch = "refs/heads/before", .Base = "refs/heads/main"});
	windows.Open(ReviewTarget{.Branch = "refs/heads/after", .Base = "refs/heads/main"});
	windows.NoteRename("refs/heads/before", "refs/heads/after");

	const std::vector<BranchRename> followed =
		windows.FollowBranches({BranchNamed("refs/heads/main"), BranchNamed("refs/heads/after")}, model);

	CHECK(windows.GetCount() == 1);
	CHECK(windows.IsOpen("refs/heads/after"));
	REQUIRE(followed.size() == 1);
	CHECK(followed.front().To == "refs/heads/after");
}

TEST_CASE("A rename of a tab's base is followed by the tab's base")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	Graphite::ReviewModel model{port, jobs.Submitter(), NoWatches()};
	ReviewWindows windows;
	const Graphite::ReviewKey oldKey{.Branch = "refs/heads/side", .Base = "refs/heads/main"};
	model.RequestRange(oldKey);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.016f});
	REQUIRE(model.GetRange(oldKey) != nullptr);
	windows.Open(ReviewTarget{.Branch = oldKey.Branch, .Base = oldKey.Base});
	windows.NoteRename("refs/heads/main", "refs/heads/trunk");

	const std::vector<BranchRename> followed =
		windows.FollowBranches({BranchNamed("refs/heads/trunk"), BranchNamed("refs/heads/side")}, model);

	REQUIRE(followed.size() == 1);
	CHECK(windows.IsOpen("refs/heads/side"));
	REQUIRE(windows.CaptureState().size() == 1);
	CHECK(windows.CaptureState().front().Base == std::optional<std::string>{"refs/heads/trunk"});
	CHECK(model.GetRange(oldKey) == nullptr);
}

TEST_CASE("Closing every tab forgets what the model held for them")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	Graphite::ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const Graphite::ReviewKey key{.Branch = "refs/heads/side", .Base = "refs/heads/main"};
	model.RequestRange(key);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{0.016f});
	ReviewWindows windows;
	windows.Open(ReviewTarget{.Branch = key.Branch, .Base = key.Base});

	windows.CloseAll(model);

	CHECK(windows.GetCount() == 0);
	CHECK(model.GetRange(key) == nullptr);
}

TEST_CASE("A review tab is named for its branch and identified by the full name")
{
	CHECK(Graphite::ReviewWindowIdentity("refs/heads/feature/x") == "review:refs/heads/feature/x");

	const std::string name = Graphite::ReviewWindowName("refs/heads/feature/x");
	CHECK(name.find("Review: feature/x###review:refs/heads/feature/x") != std::string::npos);
}
