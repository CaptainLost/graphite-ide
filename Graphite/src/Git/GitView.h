#pragma once

#include "Git/CommitLabels.h"
#include "Git/CommitListView.h"
#include "Git/DiffView.h"
#include "Git/DiffWindows.h"
#include "Git/GitModel.h"
#include "Git/HistoryFilter.h"
#include "Theme/ActiveScheme.h"

#include "Core/Events/EventBus.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Ref.h"
#include "Core/UI/Panel.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

namespace Graphite {

	// "main", "* main  2/3" for the checked-out branch with a measured distance from its
	// upstream. A pure function of the branch, pulled out of the draw loop so it can be
	// checked without a frame to draw into.
	std::string RefLabel(const Core::Branch& branch);

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
		GitView(GitModel& model, const ActiveScheme& scheme, DiffWindows& diffWindows);

		void OnImGuiRender() override;

		// Resets the first-log latch on every open and close, so a folder opened after
		// another one leaves - which drops the model's log the same way a close does -
		// gets asked for again rather than leaving the history column loading forever. Also
		// resets the commit list's own expansion, selection and load-more state, since
		// GitModel::Clear() drops every cached diff on the same two events and a selection
		// or expansion naming a commit from the old repository would otherwise point at
		// nothing the new page ever re-requests.
		void SubscribeTo(Core::EventBus& events);

	private:
		void RenderRefs(const std::vector<Core::Branch>& branches);
		void RenderCommits(const GitPalette& palette);
		void RenderDiff(const GitPalette& palette);

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

		// Referenced rather than owned: GraphiteLayer owns the one DiffWindows and renders
		// it itself, beside m_DocumentWindows, so a tab opened from here keeps drawing after
		// this panel closes or loses focus.
		DiffWindows& m_DiffWindows;

		CommitListView m_Commits;
		DiffView m_Diff;

		std::optional<std::string> m_SelectedRef;

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
		std::shared_ptr<const Core::LogPage> m_PageWhenAsked;
		bool m_Searching = false;

		// The page the list drew last frame, so a page that arrived since can be told from
		// the one already on screen. Kept apart from m_PageWhenAsked, which answers a
		// narrower question — whether the query this view sent has come back — and is unset
		// while no query is outstanding.
		std::shared_ptr<const Core::LogPage> m_LastSeenPage;

		// How tall the commit list is; the diff takes what is left. Persisted across frames by
		// DrawSplitResults's own contract — see SplitResults.h for why it starts at zero and
		// survives the diff pane being dismissed and brought back.
		float m_ListHeight = 0.0f;

		// Set once per workspace, the first frame the panel draws with no log snapshot yet
		// — an empty Ref means HEAD. Guards RequestLog so it fires once rather than every
		// frame the log is still in flight. Reset by SubscribeTo's handlers whenever the
		// workspace opens or closes, since GitModel::Clear() drops the log on both.
		bool m_LogRequested = false;

		// Which commits were expanded as of last frame, so a commit dropped from this set
		// this frame was just collapsed. Compared rather than driven by a callback out of
		// CommitListView, which must not know the model it would otherwise call into.
		std::unordered_set<Core::Oid> m_PreviouslyExpanded;

		// The whole-file texts for whichever file is currently selected, the tokens lexed from
		// them, and which file they are for. Held here rather than inside DiffView because
		// DiffView must not reach for the model — RenderDiff calls Update, which does the
		// asking, and hands the texts and tokens down to DiffView::Render as plain data. The
		// detached tabs hold one of these per tab, for the same reason and by the same rules.
		FileTextsBinding m_Texts;
	};
}
