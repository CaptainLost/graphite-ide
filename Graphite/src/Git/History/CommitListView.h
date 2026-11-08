#pragma once

#include "Core/Base/Base.h"

#include "Git/History/CommitGraphView.h"
#include "Git/History/CommitRefs.h"
#include "Git/History/FoldRow.h"
#include "Theme/GitPalette.h"
#include "Git/History/GraphGeometry.h"

#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/GraphLayout.h"

#include <imgui.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Graphite {

	struct FileSelection
	{
		Core::Oid Commit;
		std::filesystem::path Path;
	};

	// WARNING: a selection survives or not by its commit, never by its file, because the diff naming the file may not have arrived yet.
	std::optional<FileSelection> SelectionSurviving(const std::optional<FileSelection>& selection,
													const Core::LogPage& page);

	std::optional<Core::Oid> CommitSelectionSurviving(const std::optional<Core::Oid>& commit,
													  const Core::LogPage& page);

	struct CommitAction
	{
		enum class Kind
		{
			Checkout,
			NewBranch,
			Copy
		};

		Kind Action = Kind::Checkout;
		Core::Oid Commit;
		std::string Text;
		std::string What;
	};

	// What the history shows besides its commits.
	struct HistoryMarks
	{
		// The checked-out branch's or HEAD's history: the Local changes row comes first and says this after its name.
		std::optional<std::string> LocalChanges;

		std::optional<FoldRow> Fold;

		// Drawn dimmed: the line's newer commits shown beside the branch. Null for none.
		const std::unordered_set<Core::Oid>* Dimmed = nullptr;

		// Said by the status line under the list, before its "Fold them" link; the list does not draw it. Empty for none.
		std::string FoldFooter;

		// The page is on its way: one waiting row saying this stands in place of the commits.
		std::optional<std::string> Waiting;

		// A sentence about the shown reference, one row under the Local changes row, cut to the row and whole in its
		// tooltip. Empty for none.
		std::optional<std::string> Banner;

		// A word after the banner's sentence that does something when clicked, answered by TakeBannerActionClick. Only with
		// a Banner.
		std::optional<std::string> BannerAction;

		// The action is drawn disabled and a click on it is not reported.
		bool BannerActionDisabled = false;

		// Why the action is disabled, said in its tooltip; empty for nothing to say.
		std::string BannerActionNote;

		// Set in the Merges history: an opened merge lists the commits it brought, which this answers (null while they
		// are being read), instead of its files.
		std::function<Core::Ref<const Core::LogPage>(const Core::Oid&)> BroughtOf;

		// Why the commits an opened merge brought could not be read; none while they were read or are on their way.
		std::function<std::optional<std::string>(const Core::Oid&)> BroughtFailure;
	};

	// What a commit row draws of the merge it is, or of the merge it was brought by.
	struct BroughtDrawing
	{
		const Core::LogPage* Page = nullptr; // an opened merge's: the commits it brought; adds their count to its text
		bool IsBrought = false;              // the row is one of the commits a merge brought
		bool OpensBrought = false;           // the row is a merge of the Merges history, whose chevron is always drawn
	};

	// The page a merge row's count of commits is read from: held while the merge is closed too.
	Core::Ref<const Core::LogPage> BroughtCountedFor(const HistoryMarks& marks, const Core::CommitSummary& commit);

	class CommitListView
	{
	public:
		static constexpr float kDimmedRowAlpha = 0.5f;
		static constexpr float kDimmedRowPointedAlpha = 0.8f; // hovered or selected

		void Render(const Core::LogPage& page, const Core::CommitGraph& graph,
					const GitPalette& palette, const std::chrono::time_zone* zone,
					const std::function<Core::Ref<const Core::Diff>(const Core::Oid&)>& diffOf,
					const std::function<bool(const Core::Oid&)>& diffFailed, const CommitRefMap& refs,
					std::string_view highlight = {},
					std::string_view pathHighlight = {},
					const HistoryMarks& marks = {});

		// A commit a merge brought stays selected while its merge is in the page and open.
		void KeepSelectionWithin(const Core::LogPage& page, const HistoryMarks& marks = {});

		void ClearSelection() { m_SelectedFile.reset(); }

		// Lets go of the selected row: a commit, or Local changes.
		void ClearRowSelection();

		bool IsExpanded(const Core::Oid& commit) const;

		// Every commit opened, in any history shown since the folder opened.
		const std::unordered_set<Core::Oid>& GetExpanded() const { return m_Expanded; }

		void ToggleExpanded(const Core::Oid& commit);
		void SetExpanded(const Core::Oid& commit, bool expanded);

		// Selects the commit as a click on its row does, which lets go of the selected file and of Local changes.
		void SelectCommit(const Core::Oid& commit);

		void SelectFile(const FileSelection& file);

		// Selects the Local changes row as a click on it does; it is let go of again by a Render without the row.
		void SelectLocalChanges();

		bool IsLocalChangesSelected() const { return m_LocalChangesSelected; }

		// Brings the commit's row into view on the next Render that draws a page holding it; a page
		// without it drops the request.
		void ScrollToCommit(const Core::Oid& commit) { m_ScrollTarget = commit; }

		// Scrolls the list to its first row on the next Render that draws rows.
		void ScrollToTop() { m_ScrollToTop = true; }

		std::optional<FileSelection> GetSelectedFile() const { return m_SelectedFile; }

		std::optional<Core::Oid> GetSelectedCommit() const { return m_SelectedCommit; }

		// WARNING: expansions and selections name commits of the old repository once a workspace opens or closes, so all of them are cleared.
		void Reset();

		std::optional<CommitAction> TakeCommitAction();

		// Whether the fold row, or the Local changes row, was clicked since the last ask; each click is answered once.
		bool TakeFoldClick();

		// Whether the banner's action was clicked since the last ask; answered once.
		bool TakeBannerActionClick();
		bool TakeLocalChangesClick();

		void SetActionsEnabled(bool enabled) { m_ActionsEnabled = enabled; }

		// WARNING: The list stops this far above the bottom of its space, so a line drawn after it
		// stays in view rather than below the scrolled table.
		void SetRoomBelow(float height) { m_RoomBelow = height; }

		void SetEmptyText(std::string_view text) { m_EmptyText = text; }

		// The ring of the Local changes row; the modified-line colour of the palette until it is set.
		void SetLocalChangesInk(ImU32 ink) { m_LocalChangesInk = ink; }

	private:
		bool IsPointedAt(const Core::Oid& commit) const { return m_SelectedCommit == commit || m_HoveredCommit == commit; }

		bool ListedUnderOpenMerge(const Core::Oid& commit, const Core::LogPage& page, const HistoryMarks& marks) const;

		std::size_t FileRowCountUnder(
			const Core::LogPage& page, std::size_t commitIndex,
			const std::function<Core::Ref<const Core::Diff>(const Core::Oid&)>& diffOf) const;

		void RenderCommitRow(const Core::LogPage& page, const Core::CommitSummary& commit,
							 const GitPalette& palette, const CommitRefMap& refs,
							 std::string_view highlight, std::chrono::sys_seconds now,
							 const std::chrono::time_zone* zone, bool dimmed, const BroughtDrawing& brought);

		void RenderBroughtNote(const Core::LogPage* brought, const std::optional<std::string>& failure);

		// Entered in the graph column, where the ring for the work in progress is drawn around `dotCentre`.
		void RenderLocalChangesRow(const std::string& summary, const GitPalette& palette, ImVec2 dotCentre);

		void RenderFoldRow(const FoldRow& fold);
		void RenderBannerRow(const std::string& banner, const std::optional<std::string>& action, bool actionDisabled,
							 const std::string& actionNote);

		void RenderNoteRow(const std::optional<std::string>& waiting);

		void RenderFileRow(const Core::CommitSummary& commit, std::size_t fileIndex,
						   const GitPalette& palette,
						   const std::function<Core::Ref<const Core::Diff>(const Core::Oid&)>& diffOf,
						   const std::function<bool(const Core::Oid&)>& diffFailed,
						   std::string_view pathHighlight);

		std::unordered_set<Core::Oid> m_Expanded;
		std::optional<FileSelection> m_SelectedFile;
		std::optional<Core::Oid> m_SelectedCommit;
		bool m_LocalChangesSelected = false;
		std::optional<Core::Oid> m_ScrollTarget;
		bool m_ScrollToTop = false;

		// The commit under the pointer when the last Render drew it, which the graph reads.
		std::optional<Core::Oid> m_HoveredCommit;

		bool m_FoldClicked = false;
		bool m_BannerActionClicked = false;
		bool m_LocalChangesClicked = false;

		std::optional<CommitAction> m_CommitAction;
		bool m_ActionsEnabled = true;
		float m_RoomBelow = 0.0f;
		std::string m_EmptyText = "No commits yet.";
		ImU32 m_LocalChangesInk = 0;
	};
}
