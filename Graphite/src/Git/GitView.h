#pragma once

#include "Core/Base/Base.h"

#include "UI/ModalGate.h"
#include "UI/ModalQuestion.h"
#include "Git/CheckoutQuestion.h"
#include "Git/History/CommitLabels.h"
#include "Git/History/CommitListView.h"
#include "Git/History/CommitDetailsView.h"
#include "Git/Diff/DiffView.h"
#include "Git/Diff/DiffWindows.h"
#include "Editor/EditorSettings.h"
#include "Git/EditorBuffers.h"
#include "Git/GitModel.h"
#include "Review/ReviewFiles.h"
#include "Git/History/HistoryFilter.h"
#include "Theme/ActiveScheme.h"

#include "Core/Events/EventBus.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Checkout.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Stash.h"
#include "Core/UI/Panel.h"

#include <imgui.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Graphite {

	// "main", "* main  2/3" for the checked-out branch with a measured distance from its
	// upstream. A pure function of the branch, pulled out of the draw loop so it can be
	// checked without a frame to draw into.
	std::string RefLabel(const Core::Branch& branch);

	// `text` as it fits in what is left of the current line: whole, or cut with an ellipsis
	// the way the commit list cuts its cells. `cut`, when given, says which.
	std::string FitToLine(std::string_view text, bool* cut = nullptr);

	// The whole history in one panel: refs down the left, and on the right the commit list
	// above the diff of whichever file is selected under an expanded commit.
	//
	// A panel rather than a document tab: this view is keyed by selection state (ref,
	// commit, file), not by a single path on disk, so it does not fit as one entry of
	// DocumentSet the way a file does — a diff of one file in one commit can, though, which
	// is what m_DiffWindows is for. Docked into the centre, it fills the same area a
	// document would.
	class GitView final : public Core::Panel
	{
	public:
		GitView(GitModel& model, const ActiveScheme& scheme, EditorSettings& editorSettings,
				DiffWindows& diffWindows, ModalGate& modals, EditorBuffers& buffers,
				Core::EventBus& events);

		// Drains the model's one-shots that belong to this panel - a checkout plan, a delete
		// check, the paths a checkout changed - and hands the gate back for a question this
		// panel can no longer show. Here rather than in OnImGuiRender for the reasons
		// ChangesPanel::OnUpdate gives: this runs every frame, drawn or not.
		void OnUpdate(Core::Timestep ts) override;

		void OnImGuiRender() override;

		// Resets the first-log latch on every open and close, so a folder opened after
		// another one leaves - which drops the model's log the same way a close does -
		// gets asked for again rather than leaving the history column loading forever. Also
		// resets the commit list's own expansion, selection and load-more state, since
		// GitModel::Clear() drops every cached diff on the same two events and a selection
		// or expansion naming a commit from the old repository would otherwise point at
		// nothing the new page ever re-requests.
		// On the bus the constructor was given.
		void Subscribe();

		// Opens the review a branch's menu offers. Set once by whoever owns the review tabs; unset
		// hides the menu item.
		std::function<void(const ReviewOffer&)> OnReview;

		// Told of every rename sent from here, by full name, so what is kept by branch can follow it
		// once the branch list shows the new name.
		std::function<void(const std::string& from, const std::string& to)> OnBranchRenamed;

	private:
		// What the checkout dialog asks about. Two asks about one target are one question.
		struct CheckoutQuestionPayload
		{
			Core::CheckoutTarget Target;
			CheckoutQuestion Question;

			// The editor's unsaved documents as they were when the plan answered: what Discard
			// reverts and Stash saves first.
			std::vector<std::filesystem::path> UnsavedAbsolute;

			// The checkbox's starting state - ticked, and locked, when an untracked file blocks.
			bool IncludeUntracked = false;

			bool IsSameQuestion(const CheckoutQuestionPayload& other) const { return Target == other.Target; }
		};

		enum class BranchNameMode
		{
			New,
			Rename
		};

		// What the branch-name dialog starts from. The field and the checkbox the reader then
		// edits live in m_BranchNameBuffer and m_CheckoutAfter: a payload is only ever handed
		// back read-only.
		struct BranchNamePayload
		{
			BranchNameMode Mode = BranchNameMode::New;

			// The branch being renamed, or what a new branch starts from as the reader knows
			// it: a branch name or a short commit id.
			std::string From;

			// The commit a new branch starts at.
			Core::Oid At;

			std::array<char, 256> Buffer{};
			bool CheckoutAfter = true;

			bool IsSameQuestion(const BranchNamePayload& other) const
			{
				return Mode == other.Mode && From == other.From && At == other.At;
			}
		};

		struct DeleteBranchPayload
		{
			std::string Branch;

			// Commits only this branch reaches, which the delete would leave without a name.
			std::size_t Unmerged = 0;

			bool IsSameQuestion(const DeleteBranchPayload& other) const { return Branch == other.Branch; }
		};

		// Asked before a commit is checked out, which leaves HEAD on no branch.
		struct DetachPayload
		{
			Core::Oid Commit;

			bool IsSameQuestion(const DetachPayload& other) const { return Commit == other.Commit; }
		};

		struct DropStashPayload
		{
			std::size_t Index = 0;
			Core::Oid Id;

			// As git recorded it, for the question to name what it would delete.
			std::string Message;

			bool IsSameQuestion(const DropStashPayload& other) const { return Id == other.Id; }
		};

		// The three lists down the refs column, each under a heading that folds it away:
		// a repository with hundreds of remote branches otherwise buries the stashes below.
		enum class RefSection : std::size_t
		{
			Local,
			Remote,
			Stashes,
			Count
		};

		void RenderRefs(const std::vector<Core::Branch>& branches);
		void RenderRefMenu(const Core::Branch& branch);

		// The review this branch is offered, if any, as the menu's last item. Not a write, so it is
		// not greyed out while one is in flight.
		void RenderReviewItem(const Core::Branch& branch);

		// One section's heading, with how many rows it holds, as a row that folds the section
		// on a click. Returns whether the section is open.
		bool RenderRefSection(const char* heading, std::size_t count, RefSection section);

		// In GitViewStashes.cpp.
		void RenderStashes();
		void RenderStashMenu(const Core::StashEntry& stash);

		// A stash is picked the way a branch is: the history shows it - alone, since it
		// belongs to no branch - and nothing else changes until its row there is clicked.
		// Picking a branch lets go of it.
		void SelectStash(const Core::Oid& stash);
		void DeselectStash();

		// The first step of every checkout - a new branch's "Checkout after creating"
		// included: ask the model what the checkout would touch. OnUpdate takes it from there.
		void BeginCheckout(const Core::CheckoutTarget& target);
		// `suggested` pre-fills the field; empty leaves it empty.
		void BeginNewBranch(std::string from, const Core::Oid& at, std::string_view suggested = {});
		void BeginRename(const std::string& branch);

		// What a commit row's menu asked for, acted on.
		void ApplyCommitAction();

		// Keeps the history column on a branch that still exists. Run when a new branches
		// snapshot lands: a selected branch that was renamed is followed to its new name,
		// and one that is gone - deleted here or elsewhere - gives way to HEAD, since asking
		// for it again would only show an error.
		void FollowSelectedRef();

		// The plan answered: note what the editor holds unsaved now, then check out at once
		// or ask. Noted here rather than at the click, so a document edited while the plan
		// was being made is part of the decision too.
		void ApplyCheckoutPlan(CheckoutPlanAnswer answer);

		// Asks unless the same question is already asked: a second click on a menu item while
		// the first is still queued or open must not ask it twice.
		template<typename Payload>
		static void AskOnce(ModalQuestion<Payload>& question, Payload payload)
		{
			if (!question.Asked(payload))
			{
				question.Ask(std::move(payload));
			}
		}

		// Every question this panel owns, abandoned and its gate handed back, with whatever
		// the reader had edited in it.
		void WithdrawQuestions();

		// In GitViewDialogs.cpp.
		ImVec4 DangerColour() const;
		void RenderCheckoutDialog();
		void RenderDetachDialog();
		void RenderBranchNameDialog(ModalQuestion<BranchNamePayload>& question);
		void RenderDeleteBranchDialog();
		void RenderDropStashDialog();
		void RenderCommits(const GitPalette& palette);
		// The pane below the list. Which of the two things it shows is decided here: a
		// selected file wins over the commit it sits under, because picking the file is the
		// narrower question and the one the reader asked most recently.
		void RenderPreview(const GitPalette& palette);

		void RenderDiff(const GitPalette& palette);

		// The reader's own offset from UTC, for the absolute timestamps this panel shows.
		// Resolved on every call rather than cached: it changes at a daylight-saving
		// boundary, and a panel that keeps drawing after one would otherwise be an hour out
		// until the application restarts.
		//
		// Zero when the time zone database is not available — which happens on a Windows
		// install without the ICU data chrono reads — so a timestamp falls back to UTC
		// rather than the pane failing to draw.
		static std::chrono::minutes LocalOffset();

		// The details of whichever commit is selected, with its change counts taken from
		// the diff the model already holds for it — expanding the row is what asked for
		// that diff, and it is the same click that made this selection.
		void RenderDetails(const Core::CommitSummary& commit, const GitPalette& palette);

		// The two boxes above the history, and the settled query they eventually ask for.
		// Drawn here rather than inside the commit list for the same reason the list does
		// not know the model: what a filter costs is a question about queries, and the list
		// draws whatever page it is handed.
		void RenderFilterBar();

		// Says how narrow the page on screen is, and — when the walk stopped at its ceiling
		// — that the answer is not the whole story. Only drawn while a filter is on: an
		// unfiltered page speaks for itself, and a count over every commit in the repository
		// is a line nobody reads twice.
		void RenderFilterSummary(const Core::LogPage& page);

		// Notices that a new page has arrived and does what that arrival implies: the wait
		// for the last query ends, and a preview whose commit the new page no longer lists
		// is closed. Called once at the top of the frame, before anything reads either, so
		// the bar, the list and the pane cannot disagree about what is on screen.
		void UpdateForNewPage();

		// Everything that asks for a page goes through here, so a filter cannot be dropped
		// by the one caller that forgot to carry it. `maxCount` absent leaves the default
		// page size; load-more is what passes one.
		void RequestLog(std::optional<std::size_t> maxCount);

		GitModel& m_Model;
		const ActiveScheme& m_Scheme;

		// Holds the word wrap for diffs, which the docked diff reads and its button sets.
		EditorSettings& m_EditorSettings;

		// Referenced rather than owned: GraphiteLayer owns the one DiffWindows and renders
		// it itself, beside m_DocumentWindows, so a tab opened from here keeps drawing after
		// this panel closes or loses focus.
		DiffWindows& m_DiffWindows;

		// The editor's side of a checkout: which documents hold unsaved edits, and saving,
		// reverting or reloading them. A seam rather than DocumentWindows, so this panel stays
		// buildable into the tests.
		EditorBuffers& m_Buffers;

		// Where a failure's toast and a copy's confirmation are published.
		Core::EventBus& m_Events;

		CommitListView m_Commits;
		DiffView m_Diff;

		std::optional<std::string> m_SelectedRef;

		// Which sections of the refs column are folded. For this run only: folding is a
		// gesture about what is on screen now, and every section opens again on a restart.
		std::array<bool, static_cast<std::size_t>(RefSection::Count)> m_CollapsedRefSections{};

		// The stash the history is walked from, in place of m_SelectedRef. By id, as the
		// writes are, because a stash's index shifts whenever another is pushed.
		std::optional<Core::Oid> m_SelectedStash;

		// The branches snapshot FollowSelectedRef last looked at, and the full names of a
		// rename of the selected branch sent since: old, then new.
		Core::Ref<const std::vector<Core::Branch>> m_LastSeenBranches;
		std::optional<std::pair<std::string, std::string>> m_PendingRename;

		// What the reader is typing, and when it has stood still long enough to be worth a
		// query. The buffers are what ImGui writes into; SettlingFilter is what decides
		// whether the model hears about it.
		std::array<char, 256> m_FilterBuffer{};
		std::array<char, 260> m_FilterPathBuffer{};
		std::array<char, 128> m_RefFilterBuffer{};

		SettlingFilter m_Filter;

		// The page that was on screen when the last query went out. While a newer answer is
		// in flight the list still draws the older page, which is the right thing to draw
		// and the wrong thing to leave unlabelled — without this the reader cannot tell a
		// filter that found these commits from one that has not been answered yet.
		//
		// Held rather than pointed at: the model drops its reference the moment the answer
		// lands, and an address compared against a freed one could match the page that
		// replaced it.
		Core::Ref<const Core::LogPage> m_PageWhenAsked;
		bool m_Searching = false;

		// The page the list drew last frame, so a page that arrived since can be told from
		// the one already on screen. Kept apart from m_PageWhenAsked, which answers a
		// narrower question — whether the query this view sent has come back — and is unset
		// while no query is outstanding.
		Core::Ref<const Core::LogPage> m_LastSeenPage;

		// How tall the commit list is; the diff takes what is left. Persisted across frames by
		// DrawSplitResults's own contract — see SplitResults.h for why it starts at zero and
		// survives the diff pane being dismissed and brought back.
		float m_ListHeight = 0.0f;

		// Set once per workspace, the first frame the panel draws with no log snapshot yet
		// — an empty Ref means HEAD. Guards RequestLog so it fires once rather than every
		// frame the log is still in flight. Reset by Subscribe's handlers whenever the
		// workspace opens or closes, since GitModel::Clear() drops the log on both.
		bool m_LogRequested = false;

		// Which commits wanted a diff as of last frame - the expanded ones and the selected
		// one - so a commit dropped from this set this frame was just collapsed or
		// deselected. Compared rather than driven by a callback out of CommitListView, which
		// must not know the model it would otherwise call into.
		std::unordered_set<Core::Oid> m_PreviouslyWanted;

		// The whole-file texts for whichever file is currently selected, the tokens lexed from
		// them, and which file they are for. Held here rather than inside DiffView because
		// DiffView must not reach for the model — RenderDiff calls Update, which does the
		// asking, and hands the texts and tokens down to DiffView::Render as plain data. The
		// detached tabs hold one of these per tab, for the same reason and by the same rules.
		FileTextsBinding m_Texts;

		// The checkout whose plan is in flight. A plan answering any other target is one this
		// panel no longer wants.
		std::optional<Core::CheckoutTarget> m_PendingCheckout;

		// The branch whose delete check is in flight, for the same reason.
		std::optional<std::string> m_PendingDelete;

		// The names on each commit of the branches snapshot they were built from, so they are
		// rebuilt when the branches change and not on every frame.
		Core::Ref<const std::vector<Core::Branch>> m_RefsOf;
		CommitRefMap m_CommitRefs;

		ModalQuestion<DetachPayload> m_DetachQuestion;
		ModalQuestion<CheckoutQuestionPayload> m_CheckoutQuestion;
		ModalQuestion<BranchNamePayload> m_NewBranchQuestion;
		ModalQuestion<BranchNamePayload> m_RenameBranchQuestion;
		ModalQuestion<DeleteBranchPayload> m_DeleteBranchQuestion;
		ModalQuestion<DropStashPayload> m_DropStashQuestion;

		// What the reader changes in an open dialog. Taken from the payload the first frame a
		// question is on screen, and let go the first frame none is, so a dialog asked again
		// starts from its payload rather than from the last one's edits.
		bool m_CheckoutDialogOpen = false;
		bool m_IncludeUntracked = false;
		bool m_StashSaveFailed = false;

		// Which of the two branch-name questions the field was filled for.
		const ModalQuestion<BranchNamePayload>* m_BranchNameFor = nullptr;
		std::array<char, 256> m_BranchNameBuffer{};
		bool m_CheckoutAfter = true;
	};
}
