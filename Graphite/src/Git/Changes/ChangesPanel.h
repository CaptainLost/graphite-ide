#pragma once

#include "UI/ModalQuestion.h"
#include "Git/Changes/ChangesRows.h"
#include "Git/Diff/DiffWindows.h"
#include "Git/GitModel.h"
#include "Theme/GitPalette.h"
#include "Theme/ActiveScheme.h"

#include "Core/UI/Panel.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Core {
	class DocumentSet;
	class EventBus;
}

namespace Graphite {

	// Forward-declared rather than included: DocumentWindows.h pulls in most of the editor,
	// and this header only needs a reference to it.
	class DocumentWindows;

	// The working tree: what is conflicted, what is staged, what is not, and the box that
	// turns the staged half into a commit.
	//
	// A panel docked beside the explorer rather than a section of GitView, because the two
	// answer different questions. GitView is a reader's view of history — which commit,
	// which file, which branch — and this is a writer's view of right now. They are wanted
	// on screen at the same time.
	class ChangesPanel final : public Core::Panel
	{
	public:
		ChangesPanel(GitModel& model, ModalGate& modals, const Core::DocumentSet& documents,
					 DocumentWindows& documentWindows, const ActiveScheme& scheme,
					 DiffWindows& diffWindows, Core::EventBus& events);

		void OnUpdate(Core::Timestep ts) override;
		void OnImGuiRender() override;

	private:
		// Draws one section's heading and, while it is open, its rows. Returns whether it
		// drew anything - an empty section draws nothing - so the next one knows whether to
		// keep its distance from one above it (`spaced`).
		bool RenderSection(const char* heading, const std::vector<std::size_t>& rows,
						   const Core::WorkingTreeStatus& status, const GitPalette& palette,
						   bool spaced);

		// One row, and everything a row can do: click to open its diff, and the buttons that
		// appear on its right edge under the mouse - discard, and the contextual stage or
		// unstage.
		void RenderRow(std::size_t entry, const Core::WorkingTreeStatus& status,
					   const GitPalette& palette);

		// An untracked directory: one row for the directory itself, and, while it is open,
		// one row per file under it. Drawn apart from RenderRow because almost nothing is
		// shared - a directory has no diff to open, no status letter worth more than 'U',
		// and a file count that does not exist until git has been asked.
		void RenderFolderRow(std::size_t entry, const Core::WorkingTreeStatus& status,
							 const GitPalette& palette);

		// The row's context menu. Same items and the same wording as the explorer's, so one
		// gesture means one thing across the application; "Open File" is the exception,
		// because the explorer opens a file by double-clicking it and this panel's click
		// is already spoken for by the diff.
		//
		// `letter` is the one the row is already drawing, passed rather than worked out
		// again: a conflicted row reads it off both halves of its status at once, and a
		// second derivation here could disagree with what the reader can see.
		void RenderRowContextMenu(const Core::FileStatus& file, char letter);

		void RenderCommitBox();

		// The error a write left behind, with a way to dismiss it. Drawn above the commit
		// box rather than as a modal: a failed stage is information, not a question.
		void RenderWriteError();

		// Which side of the file this row's diff shows. A staged row compares the index
		// against HEAD; every other row compares the disk against the index.
		Core::DiffTarget TargetForSection() const;

		// The colour the gutter gives this kind of change, so one concept is not given two
		// colour schemes.
		unsigned int ColourOfLetter(char letter, const GitPalette& palette) const;

		// Asks about discarding `file`. Never discards on the spot: git can hand back none of
		// what a discard throws away, so the question is the discard's first step and not a
		// special case of it.
		void BeginDiscard(const Core::FileStatus& file);

		void RenderDiscardConfirm();

		// Asks about stashing, and what to do first: unsaved edits are saved before the stash
		// so it takes what the reader sees on screen and not what was last on disk.
		void RenderStashDialog();

		// Drops the dialog's edits, for whichever way it ended - an answer, or a withdrawal
		// from outside.
		void ResetStashDialog();

		// The open documents with unsaved edits that live inside the repository. Others are
		// left alone: a stash cannot touch them, and saving them is not what was asked for.
		std::vector<std::filesystem::path> DirtyDocumentsInRepository() const;

		// The absolute path of every open document with unsaved changes. Which of them a
		// discard would overwrite is PlanDiscard's decision, not this panel's - all that is
		// wanted here is the list, which only the DocumentSet can give.
		std::vector<std::filesystem::path> DirtyDocuments() const;

		// Whether the selected row is a file drawn inside a folder that is currently open.
		// Such a row is drawn against the folder's own listing and so never enters
		// m_LastDrawn, which records top-level rows only - without this, any change to the
		// top-level list would find the selection in neither the before nor the after and
		// drop it, leaving the open diff tab with no row saying which file it belongs to.
		bool SelectedIsInsideExpandedFolder() const;

		GitModel& m_Model;
		const Core::DocumentSet& m_Documents;

		// Told which files a confirmed discard just touched, so the editor reloads them
		// without asking - see the ExpectExternalWrite call in OnImGuiRender.
		DocumentWindows& m_DocumentWindows;

		const ActiveScheme& m_Scheme;
		DiffWindows& m_DiffWindows;

		// Where "Committed ..." goes: the status bar says it, since the commit box clears
		// itself and would otherwise be the only sign that anything happened.
		Core::EventBus& m_Events;

		// Which section RenderRow is currently drawing, so a row knows whether its button
		// stages or unstages without every call site passing it down.
		enum class Section
		{
			Conflicted,
			Staged,
			Unstaged,

			// Not a section - the count of the ones above it. Sizes the per-section arrays below,
			// so a new section grows them instead of indexing past their end.
			Count
		};

		static constexpr std::size_t kSectionCount = static_cast<std::size_t>(Section::Count);

		Section m_Section = Section::Unstaged;

		// The selected row, as the path and the section it was drawn under - never as an
		// index into WorkingTreeStatus::Entries, which is rebuilt and reordered on every
		// refresh. See NextSelection in ChangesRows.h.
		//
		// One file can be drawn in two sections at once, staged one way and edited again
		// since, so the section is part of the identity of a row and not decoration.
		struct Selection
		{
			Section InSection = Section::Unstaged;
			std::filesystem::path Path;

			bool operator==(const Selection&) const = default;
		};

		std::optional<Selection> m_Selected;

		// Which untracked directories the reader has opened, by path. Kept across a status
		// refresh - a directory does not close because a file elsewhere was saved - and
		// cleared when the workspace closes.
		std::set<std::filesystem::path> m_Expanded;

		// What each section drew last frame, in drawn order, so a selection whose row has
		// gone can be moved to the row that took its place.
		std::array<std::vector<std::filesystem::path>, kSectionCount> m_LastDrawn;

		// Which sections the reader has closed, by Section. Open is the default, so a
		// section that appears for the first time - the first conflict of a merge - is seen.
		std::array<bool, kSectionCount> m_Collapsed{};

		std::string m_Message;
		bool m_Amend = false;

		// What one discard would throw away. Everything the question needs to word itself,
		// worked out when the discard is asked for rather than while the dialog draws -
		// which is also the only moment the folder's listing is still in reach, since by the
		// time the discard has landed the directory is gone from disk.
		struct DiscardQuestion
		{
			// What the discard covers, what it would overwrite, and how many files it names.
			// Decided by PlanDiscard, which is pure and tested - see ChangesRows.h.
			DiscardPlan Plan;

			// Named for the dialog's first line. One discard is always about one row.
			std::filesystem::path Subject;

			// Composed here rather than in the dialog, for the same reason the plan is: the
			// wording is a decision about what is being lost, and a decision a test can read.
			std::string Text;

			// Two asks about one row are one question: a second click on a discard button the
			// reader has not answered yet must not queue a duplicate behind the first.
			bool IsSameQuestion(const DiscardQuestion& other) const
			{
				return Subject == other.Subject;
			}
		};

		ModalQuestion<DiscardQuestion> m_DiscardQuestion;

		// The stash question carries nothing: what it asks is the same whenever it is asked,
		// and the reader's edits live in the members below. Every one is equal to every
		// other, so a second click on the button before the first is answered queues nothing.
		struct StashQuestion
		{
			bool operator==(const StashQuestion&) const = default;
		};

		ModalQuestion<StashQuestion> m_StashQuestion;
		std::string m_StashMessage;
		bool m_StashIncludeUntracked = false;

		// Set when saving the documents before the stash failed, and shown in the dialog,
		// which stays open.
		bool m_StashSaveFailed = false;

		// The files a requested discard is about to take with it, by the subject path git
		// will name in TakeDiscarded. Written when the reader confirms and read when the
		// discard lands, because those are two different moments: a directory is already
		// gone from disk by the time it is reported, so it cannot be enumerated then, and
		// the editor would meet the deletion of each file under it as a change it never made.
		//
		// An entry is erased as it is used. One is left behind by a discard that failed,
		// where no report ever comes; the map is cleared with everything else when the
		// repository goes away.
		std::map<std::filesystem::path, std::vector<std::filesystem::path>> m_DiscardedFiles;
	};
}
