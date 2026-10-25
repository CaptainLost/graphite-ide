#pragma once

#include "UI/Dialogs/ModalQuestion.h"
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

	class DocumentWindows;

	class ChangesPanel final : public Core::Panel
	{
	public:
		ChangesPanel(GitModel& model, ModalGate& modals, const Core::DocumentSet& documents,
					 DocumentWindows& documentWindows, const ActiveScheme& scheme,
					 DiffWindows& diffWindows, Core::EventBus& events);

		void OnUpdate(Core::Timestep ts) override;
		void OnImGuiRender() override;

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

		// Under an open folder whose files git has not listed: the loader, or why the listing failed.
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
		DiffWindows& m_DiffWindows;

		Core::EventBus& m_Events;

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

		std::string m_Message;
		bool m_Amend = false;

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

		// WARNING: Written when a discard is confirmed and read when it lands, because a directory
		// is gone from disk by then. An entry is erased as it is used; a failed discard leaves one
		// behind, and the map is cleared when the repository goes away.
		std::map<std::filesystem::path, std::vector<std::filesystem::path>> m_DiscardedFiles;
	};
}
