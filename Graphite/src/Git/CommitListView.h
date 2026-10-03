#pragma once

#include "Core/Base/Base.h"

#include "Git/CommitGraphView.h"
#include "Git/GitPalette.h"
#include "Git/GraphGeometry.h"

#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/GraphLayout.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace Graphite {

	// Which file is being shown below the list. Carries the commit as well as the path,
	// because with several rows expanded the same path can appear under more than one.
	struct FileSelection
	{
		Core::Oid Commit;
		std::filesystem::path Path;
	};

	// The selection a newly arrived page leaves standing. A selection survives while the
	// commit it names is still listed: a refresh that still shows the commit has not taken
	// the reader anywhere, and closing the diff they were reading would be the panel losing
	// their place for them. A narrowed filter that drops the commit is the other case — the
	// pane would be answering a question nothing on screen asks any more.
	//
	// Checked against the commit and not the file: whether the file is in that commit's diff
	// is a question the diff pane already answers for itself, and it cannot answer this one,
	// because the diff may not have arrived yet.
	std::optional<FileSelection> SelectionSurviving(const std::optional<FileSelection>& selection,
													const Core::LogPage& page);

	// The same rule for the commit the details pane is showing. Kept apart from the file
	// selection above because the two are set by different gestures and one can outlive the
	// other, but answered the same way: a commit the new page no longer lists is a commit
	// nothing on screen points at any more.
	std::optional<Core::Oid> CommitSelectionSurviving(const std::optional<Core::Oid>& commit,
													  const Core::LogPage& page);

	// What the reader asked of one commit row through its context menu. Handed out rather
	// than acted on, because acting means asking the model, which the list does not know.
	struct CommitAction
	{
		enum class Kind
		{
			Checkout,
			NewBranch
		};

		Kind Action = Kind::Checkout;
		Core::Oid Commit;
	};

	// A list of commits with the graph down its left edge. Used by the Git view, and by the
	// file and line histories in the next plan — which is why it takes a LogPage and a graph
	// rather than reaching for a model.
	class CommitListView
	{
	public:
		// `offset` is the reader's distance from UTC, for the exact time the tooltip over a
		// date gives. Handed in because the list does not read the time zone itself.
		//
		// `diffOf` answers a commit's diff, or null while it is being fetched. The list
		// asks for every commit it draws; the model is what decides whether that costs a
		// query.
		//
		// `highlight` is marked in each commit row wherever it appears — the same text the
		// history was filtered by, so the reader can see which part of the row answered
		// their question rather than re-reading the row to find out. Empty marks nothing.
		//
		// `pathHighlight` does the same for the file rows under an expanded commit. Kept
		// apart from `highlight` because the two boxes above the list are separate
		// questions, and the one that narrowed the files is the one a file row should
		// answer. A pattern such as "*.cpp" marks nothing, since it appears nowhere in the
		// path it selected — which is the honest outcome rather than marking the extension
		// the path happens to end with.
		void Render(const Core::LogPage& page, const Core::CommitGraph& graph,
					const GitPalette& palette, std::chrono::minutes offset,
					const std::function<Core::Ref<const Core::Diff>(const Core::Oid&)>& diffOf,
					std::string_view highlight = {}, std::string_view pathHighlight = {});

		// Applied when a new page arrives, by whoever noticed it arrive — the list is handed
		// pages and does not know one is newer than the last. See SelectionSurviving for
		// which selections a page leaves standing and why.
		void KeepSelectionWithin(const Core::LogPage& page);

		// Closes the preview without touching the expansion set: the reader dismissed one
		// file, not the commit they opened to find it. The commit selection stays, so the
		// pane falls back to that commit's details rather than to nothing.
		void ClearSelection() { m_SelectedFile.reset(); }

		// Drops the commit the details pane is describing, and nothing else — the reader
		// dismissed a description, not the file they may also have open. Mirrors
		// ClearSelection above, which does the same for the other half.
		void ClearCommitSelection() { m_SelectedCommit.reset(); }

		bool IsExpanded(const Core::Oid& commit) const;
		void ToggleExpanded(const Core::Oid& commit);

		std::optional<FileSelection> GetSelectedFile() const { return m_SelectedFile; }

		// The commit whose details belong in the pane below. Set by clicking anywhere on a
		// commit row except its arrow, which expands and collapses and does nothing else —
		// so reading a commit's message and opening its file list are separate gestures, and
		// neither undoes the other. Cleared by nothing except a new page dropping it, or by
		// Reset.
		std::optional<Core::Oid> GetSelectedCommit() const { return m_SelectedCommit; }

		// Clears the expansion set, both selections and the load-more latch. Called when
		// the workspace opens or closes: GitModel::Clear() drops every cached diff on both,
		// so an expansion or a selection naming a commit from the old repository would point
		// at nothing the new page ever re-requests, and RenderDiff would show "Loading…"
		// forever. The latch needs the same reset - a new repository whose first page happens
		// to match the last armed commit count would otherwise never arm load-more.
		void Reset();

		// True on the first frame, after a page arrives, that finds the reader at the
		// bottom of an incomplete page. Latched on the page's own commit count rather than
		// merely consumed: consuming only stops one arming from being read twice, it does
		// not stop the widget arming again next frame while the reader is still sitting at
		// the bottom and the bigger page has not arrived yet. Latching on the count means a
		// caller that ignores a request will not be nagged — a caller that ignored one
		// request would ignore sixty, so a second ask carries no information.
		bool TakeLoadMoreRequest();

		// One-shot: the last action picked from a commit row's context menu, once.
		std::optional<CommitAction> TakeCommitAction();

		// Greys the context menu's items, for while a write is running. The list does not
		// know the model that knows, so the owner says so before each Render.
		void SetActionsEnabled(bool enabled) { m_ActionsEnabled = enabled; }

	private:
		void RenderCommitRow(const Core::LogPage& page, const Core::CommitSummary& commit,
							 const GitPalette& palette, std::string_view highlight,
							 std::chrono::sys_seconds now, std::chrono::minutes offset);

		void RenderFileRow(const Core::CommitSummary& commit, std::size_t fileIndex,
						   const GitPalette& palette,
						   const std::function<Core::Ref<const Core::Diff>(const Core::Oid&)>& diffOf,
						   std::string_view pathHighlight);

		std::unordered_set<Core::Oid> m_Expanded;
		std::optional<FileSelection> m_SelectedFile;
		std::optional<Core::Oid> m_SelectedCommit;

		bool m_LoadMoreRequested = false;

		// The page size the last load-more request was armed at. -1 so the very first
		// incomplete page (size 0 is impossible here, Render returns early on empty) can
		// still arm.
		std::ptrdiff_t m_LoadMoreRequestedAtCount = -1;

		std::optional<CommitAction> m_CommitAction;
		bool m_ActionsEnabled = true;
	};
}
