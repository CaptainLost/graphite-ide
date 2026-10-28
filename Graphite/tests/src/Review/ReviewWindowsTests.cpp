#include <doctest/doctest.h>

#include "Review/ReviewWindows.h"

#include "DeferredJobs.h"
#include "ImGuiFrames.h"
#include "Review/FakeReviewPort.h"
#include "UI/Dialogs/ModalGate.h"

#include "Core/ImGui/TabCloseRequests.h"

#include <imgui.h>

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

	const Core::Timestep kFrame{0.016f};

	void HoldRange(Graphite::ReviewModel& model, DeferredJobs& jobs, const ReviewKey& key)
	{
		model.RequestRange(key);
		jobs.RunAll();
		model.OnUpdate(kFrame);
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

namespace {

	const std::string kOther = "refs/heads/other";

	constexpr float kHostWidth = 1000.0f;
	constexpr float kHostHeight = 600.0f;
	constexpr int kFramesToSettle = 3;

	// The window an embedded review is drawn into, as the Git window's main area holds it.
	struct EmbeddedHost
	{
		ImGuiFrames Frames;
		FakeReviewPort Port;
		DeferredJobs Jobs;
		Graphite::ReviewModel Model{Port, Jobs.Submitter(), NoWatches()};
		ReviewSessions Sessions;
		Graphite::ModalGate Modals;
		ReviewWindows Windows{Modals};
		Graphite::GitPalette Palette;
		Graphite::SyntaxPalette Syntax;
		Core::TabCloseRequests CloseRequests;

		Graphite::ReviewRenderInput Input()
		{
			return Graphite::ReviewRenderInput{.Model = Model,
											   .Sessions = Sessions,
											   .Palette = Palette,
											   .Syntax = Syntax,
											   .CloseRequests = CloseRequests};
		}

		// Draws frame after frame, landing what each one asked for, until nothing more is asked.
		void Draw(const std::string& branch)
		{
			const Graphite::ReviewRenderInput input = Input();

			for (int frame = 0; frame < kFramesToSettle; ++frame)
			{
				Frames.Next([&] {
					ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
					ImGui::SetNextWindowSize(ImVec2(kHostWidth, kHostHeight));
					ImGui::Begin("Git", nullptr, ImGuiWindowFlags_NoSavedSettings);
					Windows.RenderEmbedded(branch, input);
					ImGui::End();
				});

				Jobs.RunAll();
				Model.OnUpdate(kFrame);
			}
		}

		void CloseTab(const std::string& branch)
		{
			const Graphite::ReviewRenderInput input = Input();

			CloseRequests.Request(Graphite::ReviewWindowName(branch));
			CloseRequests.EndFrame();
			Frames.Next([&] { Windows.Render(input); });
		}
	};
}

TEST_CASE("An embedded review draws a draft's branch without opening a tab")
{
	EmbeddedHost host;
	host.Sessions.OpenDraft(kSide, kMain);

	host.Draw(kSide);

	CHECK(host.Windows.GetCount() == 0);
	CHECK(host.Windows.CaptureState().empty());
	CHECK(host.Port.RangeQueries == 1);
	CHECK(host.Port.RangeDiffQueries == 1);
}

TEST_CASE("An embedded review of a branch that is gone asks to be closed")
{
	EmbeddedHost host;
	host.Port.Range.Problem = Graphite::RangeProblem::BranchMissing;
	host.Sessions.OpenDraft(kSide, kMain);

	host.Draw(kSide);

	CHECK(host.Windows.TakeEmbeddedClosed());
	CHECK_FALSE(host.Windows.TakeEmbeddedClosed());
}

TEST_CASE("Switching the embedded review to another branch asks that branch's range")
{
	EmbeddedHost host;
	host.Sessions.OpenDraft(kSide, kMain);
	host.Sessions.OpenDraft(kOther, kMain);
	const ReviewKey sideKey{.Branch = kSide, .Base = kMain};

	host.Draw(kSide);
	REQUIRE(host.Model.GetRange(sideKey) != nullptr);

	host.Draw(kOther);

	CHECK(host.Port.RangeQueries == 2);
	CHECK(host.Model.GetRange(sideKey) == nullptr);
	CHECK(host.Model.GetRange(ReviewKey{.Branch = kOther, .Base = kMain}) != nullptr);
}

TEST_CASE("Switching the embedded review away from a branch a tab still shows keeps that tab's range")
{
	EmbeddedHost host;
	host.Sessions.Start(kSide, kMain);
	host.Sessions.OpenDraft(kOther, kMain);
	const ReviewKey sideKey{.Branch = kSide, .Base = kMain};
	host.Windows.Open(kSide);
	host.Windows.FollowSessions(host.Sessions, host.Model);

	host.Draw(kSide);
	REQUIRE(host.Model.GetRange(sideKey) != nullptr);

	host.Draw(kOther);

	CHECK(host.Model.GetRange(sideKey) != nullptr);
	CHECK(host.Port.RangeQueries == 2);
}

TEST_CASE("Closing a review tab keeps the range the embedded review of its branch still shows")
{
	EmbeddedHost host;
	host.Sessions.Start(kSide, kMain);
	const ReviewKey sideKey{.Branch = kSide, .Base = kMain};
	host.Windows.Open(kSide);
	host.Draw(kSide);
	REQUIRE(host.Model.GetRange(sideKey) != nullptr);

	host.CloseTab(kSide);

	CHECK(host.Windows.GetCount() == 0);
	CHECK(host.Model.GetRange(sideKey) != nullptr);
}

TEST_CASE("An embedded review whose session ends forgets what the model held for it")
{
	EmbeddedHost host;
	host.Sessions.OpenDraft(kSide, kMain);
	const ReviewKey key{.Branch = kSide, .Base = kMain};
	host.Draw(kSide);
	REQUIRE(host.Model.GetRange(key) != nullptr);

	host.Sessions.End(kSide);
	host.Windows.FollowSessions(host.Sessions, host.Model);

	CHECK(host.Model.GetRange(key) == nullptr);
}

TEST_CASE("An embedded review follows a rename and forgets the range it held under the old name")
{
	EmbeddedHost host;
	host.Sessions.OpenDraft("refs/heads/before", kMain);
	const ReviewKey oldKey{.Branch = "refs/heads/before", .Base = kMain};
	host.Draw("refs/heads/before");
	REQUIRE(host.Model.GetRange(oldKey) != nullptr);
	host.Windows.NoteRename("refs/heads/before", "refs/heads/after");

	const std::vector<BranchRename> followed =
		host.Windows.FollowBranches({BranchNamed(kMain), BranchNamed("refs/heads/after")}, host.Model);

	REQUIRE(followed.size() == 1);
	CHECK(host.Model.GetRange(oldKey) == nullptr);
	CHECK(host.Windows.GetCount() == 0);

	host.Sessions.Rename(followed.front().From, followed.front().To);
	host.Draw("refs/heads/after");

	CHECK(host.Port.RangeQueries == 2);
	CHECK(host.Model.GetRange(ReviewKey{.Branch = "refs/heads/after", .Base = kMain}) != nullptr);
}

TEST_CASE("Closing every tab forgets what the model held for the embedded review too")
{
	EmbeddedHost host;
	host.Sessions.OpenDraft(kSide, kMain);
	const ReviewKey key{.Branch = kSide, .Base = kMain};
	host.Draw(kSide);
	REQUIRE(host.Model.GetRange(key) != nullptr);

	host.Windows.CloseAll(host.Model);

	CHECK(host.Model.GetRange(key) == nullptr);
}
