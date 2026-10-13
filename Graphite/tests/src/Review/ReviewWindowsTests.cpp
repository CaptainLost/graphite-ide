#include <doctest/doctest.h>

#include "Review/ReviewWindows.h"

#include "DeferredJobs.h"
#include "Review/FakeReviewPort.h"
#include "UI/ModalGate.h"

#include <string>
#include <vector>

using Graphite::BranchRename;
using Graphite::ReviewKey;
using Graphite::ReviewSessions;
using Graphite::ReviewTabState;
using Graphite::ReviewWindows;

namespace {

	const std::string kMain = "refs/heads/main";
	const std::string kSide = "refs/heads/side";

	Core::Branch BranchNamed(const std::string& fullName)
	{
		Core::Branch branch;
		branch.FullName = fullName;
		return branch;
	}

	const std::vector<Core::Branch> kMainAndSide = {BranchNamed(kMain), BranchNamed(kSide)};

	ReviewSessions SessionsOf(const std::vector<std::string>& branches)
	{
		ReviewSessions sessions;

		for (const std::string& branch : branches)
		{
			sessions.Start(branch, kMain);
		}

		return sessions;
	}

	// What a drawn tab leaves the model holding.
	void HoldRange(Graphite::ReviewModel& model, DeferredJobs& jobs, const ReviewKey& key)
	{
		model.RequestRange(key);
		jobs.RunAll();
		model.OnUpdate(Core::Timestep{0.016f});
	}
}

TEST_CASE("Opening a branch already under review raises its tab rather than adding one")
{
	Graphite::ModalGate modals;
	ReviewWindows windows{modals};

	windows.Open(kSide);
	windows.Open(kSide);

	CHECK(windows.GetCount() == 1);
}

TEST_CASE("Open review tabs come back as they were captured")
{
	ReviewSessions sessions = SessionsOf({kSide});
	const Graphite::SessionSettings withUncommitted{.IncludeUncommitted = true};
	sessions.SetSettings(kSide, withUncommitted);
	Graphite::ModalGate modals;
	ReviewWindows before{modals};
	before.RestoreState({ReviewTabState{.Branch = kSide, .Selected = "src/a.cpp", .SelectedIsUncommitted = true}},
						sessions);

	ReviewWindows after{modals};
	after.RestoreState(before.CaptureState(), sessions);

	REQUIRE(after.CaptureState().size() == 1);
	CHECK(after.CaptureState() == before.CaptureState());
	CHECK(after.CaptureState().front().Selected == "src/a.cpp");
}

TEST_CASE("A restored selection among uncommitted files is dropped when the session leaves them out")
{
	const ReviewSessions sessions = SessionsOf({kSide});
	Graphite::ModalGate modals;
	ReviewWindows windows{modals};

	windows.RestoreState({ReviewTabState{.Branch = kSide, .Selected = "src/a.cpp", .SelectedIsUncommitted = true}},
						 sessions);

	CHECK(windows.CaptureState().front().Selected.empty());
}

TEST_CASE("A stored tab whose branch has no session is not reopened")
{
	const ReviewSessions sessions = SessionsOf({kSide});
	Graphite::ModalGate modals;
	ReviewWindows windows{modals};

	windows.RestoreState({ReviewTabState{.Branch = kSide}, ReviewTabState{.Branch = "refs/heads/ended"}}, sessions);

	CHECK(windows.IsOpen(kSide));
	CHECK_FALSE(windows.IsOpen("refs/heads/ended"));
}

TEST_CASE("A restored tab waits for the first branch snapshot and closes only when it lacks its branch")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	Graphite::ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const ReviewSessions sessions = SessionsOf({kSide, "refs/heads/gone"});
	Graphite::ModalGate modals;
	ReviewWindows windows{modals};
	windows.RestoreState({ReviewTabState{.Branch = kSide}, ReviewTabState{.Branch = "refs/heads/gone"}}, sessions);

	CHECK(windows.GetCount() == 2);

	static_cast<void>(windows.FollowBranches(kMainAndSide, model)); // no rename was noted

	CHECK(windows.IsOpen(kSide));
	CHECK_FALSE(windows.IsOpen("refs/heads/gone"));
}

TEST_CASE("A rename is followed once the snapshot shows it and is handed on for the sessions")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	Graphite::ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const ReviewSessions sessions = SessionsOf({"refs/heads/before"});
	const ReviewKey oldKey{.Branch = "refs/heads/before", .Base = kMain};
	HoldRange(model, jobs, oldKey);
	REQUIRE(model.GetRange(oldKey) != nullptr);
	Graphite::ModalGate modals;
	ReviewWindows windows{modals};
	windows.Open("refs/heads/before");
	windows.FollowSessions(sessions, model);
	windows.NoteRename("refs/heads/before", "refs/heads/after");

	const std::vector<BranchRename> notYet =
		windows.FollowBranches({BranchNamed(kMain), BranchNamed("refs/heads/before")}, model);
	CHECK(notYet.empty());
	CHECK(windows.IsOpen("refs/heads/before"));

	const std::vector<BranchRename> followed =
		windows.FollowBranches({BranchNamed(kMain), BranchNamed("refs/heads/after")}, model);

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
	Graphite::ModalGate modals;
	ReviewWindows windows{modals};
	windows.Open("refs/heads/before");
	windows.Open("refs/heads/after");
	windows.NoteRename("refs/heads/before", "refs/heads/after");

	const std::vector<BranchRename> followed =
		windows.FollowBranches({BranchNamed(kMain), BranchNamed("refs/heads/after")}, model);

	CHECK(windows.GetCount() == 1);
	CHECK(windows.IsOpen("refs/heads/after"));
	REQUIRE(followed.size() == 1);
	CHECK(followed.front().To == "refs/heads/after");
}

TEST_CASE("A tab closes once its session ends and what the model held for it is forgotten")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	Graphite::ReviewModel model{port, jobs.Submitter(), NoWatches()};
	ReviewSessions sessions = SessionsOf({kSide});
	const ReviewKey key{.Branch = kSide, .Base = kMain};
	HoldRange(model, jobs, key);
	Graphite::ModalGate modals;
	ReviewWindows windows{modals};
	windows.Open(kSide);
	windows.FollowSessions(sessions, model);
	REQUIRE(windows.IsOpen(kSide));

	sessions.End(kSide);
	windows.FollowSessions(sessions, model);

	CHECK_FALSE(windows.IsOpen(kSide));
	CHECK(model.GetRange(key) == nullptr);
}

TEST_CASE("A tab follows its session's renamed base, forgets the range it held and keeps its selection")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	Graphite::ReviewModel model{port, jobs.Submitter(), NoWatches()};
	ReviewSessions sessions = SessionsOf({kSide});
	const ReviewKey oldKey{.Branch = kSide, .Base = kMain};
	HoldRange(model, jobs, oldKey);
	Graphite::ModalGate modals;
	ReviewWindows windows{modals};
	windows.RestoreState({ReviewTabState{.Branch = kSide, .Selected = "src/a.cpp"}}, sessions);
	windows.FollowSessions(sessions, model);

	sessions.Rename(kMain, "refs/heads/trunk");
	windows.FollowSessions(sessions, model);

	CHECK(windows.IsOpen(kSide));
	CHECK(model.GetRange(oldKey) == nullptr);
	CHECK(windows.CaptureState().front().Selected == "src/a.cpp");
}

TEST_CASE("A tab the reader opened stays when its branch is missing so it can say so")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	Graphite::ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const ReviewSessions sessions = SessionsOf({"refs/heads/gone"});
	Graphite::ModalGate modals;
	ReviewWindows windows{modals};
	windows.Open("refs/heads/gone");
	windows.FollowSessions(sessions, model);

	static_cast<void>(windows.FollowBranches(kMainAndSide, model)); // no rename was noted

	CHECK(windows.IsOpen("refs/heads/gone"));
}

TEST_CASE("Closing every tab withdraws a question about ending a review")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	Graphite::ReviewModel model{port, jobs.Submitter(), NoWatches()};
	Graphite::ModalGate modals;
	ReviewWindows windows{modals};
	windows.AskToEnd(kSide);
	REQUIRE(windows.IsAskingToEnd());

	windows.CloseAll(model);

	CHECK_FALSE(windows.IsAskingToEnd());
}

TEST_CASE("Closing every tab forgets what the model held for them")
{
	FakeReviewPort port;
	DeferredJobs jobs;
	Graphite::ReviewModel model{port, jobs.Submitter(), NoWatches()};
	const ReviewSessions sessions = SessionsOf({kSide});
	const ReviewKey key{.Branch = kSide, .Base = kMain};
	HoldRange(model, jobs, key);
	Graphite::ModalGate modals;
	ReviewWindows windows{modals};
	windows.Open(kSide);
	windows.FollowSessions(sessions, model);

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
