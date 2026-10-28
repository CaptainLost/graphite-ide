#pragma once

#include "UI/Dialogs/ModalQuestion.h"
#include "Git/Changes/ChangesRows.h"
#include "Git/Changes/ChangesShared.h"
#include "Git/Diff/DiffBody.h"
#include "Git/GitModel.h"
#include "Theme/GitPalette.h"
#include "Theme/ActiveScheme.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Core {
	class DocumentSet;
	class EventBus;
}

namespace Graphite {

	class DocumentWindows;

	// The working tree's changes, the commit box and their dialogs. Each host has a view of its own, with its own
	// selection and open folders; what the hosts share is held in ChangesShared.
	class ChangesView
	{
	public:
		ChangesView(GitModel& model, ModalGate& modals, const Core::DocumentSet& documents, DocumentWindows& documentWindows,
					const ActiveScheme& scheme, Core::EventBus& events, ChangesShared& shared);

		// The reader picked a row: the side panel opens a diff tab, the Git window shows the diff beside the list.
		std::function<void(const OpenDiff& diff)> OnFileChosen;

		// The lists, the commit box and the dialogs, into the current window.
		void Draw();

		// WARNING: A question left open while its host is not drawn would hold the modal gate forever, so a host
		// calls this on every frame it does not draw the view. Queued questions are dropped with it.
		void WithdrawQuestions();

	private:
		bool RenderSection(const char* heading, const std::vector<std::size_t>& rows,
						   const Core::WorkingTreeStatus& status, const GitPalette& palette,
						   bool spaced);

		enum class RowButtons
		{
			Offered,
			HeldForFreshListing
		};

		void RenderRow(std::size_t entryIndex, const Core::WorkingTreeStatus& status,
					   const GitPalette& palette, RowButtons buttons);

		void RenderFolderRow(std::size_t entryIndex, const Core::WorkingTreeStatus& status,
							 const GitPalette& palette);

		void RenderListingWait(const std::filesystem::path& folder);

		void RenderRowContextMenu(const Core::FileStatus& file, char letter);

		void RenderCommitBox();

		Core::DiffTarget TargetForSection() const;

		unsigned int ColourOfLetter(char letter, const GitPalette& palette) const;

		void BeginDiscard(const Core::FileStatus& file);

		void RenderDiscardConfirm();

		void RenderStashDialog();

		void ResetStashDialog();

		std::vector<std::filesystem::path> DirtyDocumentsInRepository() const;

		std::vector<std::filesystem::path> DirtyDocuments() const;

		// WARNING: A file drawn inside an open folder never enters m_LastDrawn, which holds top-level
		// rows only. Without this test a change to the top-level list would drop that selection.
		bool SelectedIsInsideExpandedFolder() const;

		GitModel& m_Model;
		const Core::DocumentSet& m_Documents;

		DocumentWindows& m_DocumentWindows;

		const ActiveScheme& m_Scheme;

		Core::EventBus& m_Events;

		ChangesShared& m_Shared;

		enum class Section
		{
			Conflicted,
			Staged,
			Unstaged,

			Count
		};

		static constexpr std::size_t kSectionCount = static_cast<std::size_t>(Section::Count);

		Section m_Section = Section::Unstaged;

		struct Selection
		{
			Section InSection = Section::Unstaged;
			std::filesystem::path Path;

			bool operator==(const Selection&) const = default;
		};

		std::optional<Selection> m_Selected;

		std::set<std::filesystem::path> m_Expanded;

		std::array<std::vector<std::filesystem::path>, kSectionCount> m_LastDrawn;

		std::array<bool, kSectionCount> m_Collapsed{};

		struct DiscardQuestion
		{
			DiscardPlan Plan;

			std::filesystem::path Subject;

			std::string Text;

			bool IsSameQuestion(const DiscardQuestion& other) const
			{
				return Subject == other.Subject;
			}
		};

		ModalQuestion<DiscardQuestion> m_DiscardQuestion;

		struct StashQuestion
		{
			bool operator==(const StashQuestion&) const = default;
		};

		ModalQuestion<StashQuestion> m_StashQuestion;
		std::string m_StashMessage;
		bool m_StashIncludeUntracked = false;

		bool m_StashSaveFailed = false;
	};
}
