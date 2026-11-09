#pragma once

#include "UI/Dialogs/ModalQuestion.h"
#include "Git/Changes/ChangesRows.h"
#include "Git/Changes/ChangesShared.h"
#include "Git/Diff/DiffBody.h"
#include "Git/EditorBuffers.h"
#include "Git/GitModel.h"
#include "Git/Worktrees/WriteExpectation.h"
#include "Core/Git/Ref.h"
#include "Theme/GitPalette.h"
#include "Theme/ActiveScheme.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Core {
	class EventBus;
}

namespace Graphite {

	// What a host lets its Changes view offer and how the view names where it commits; the defaults are the open folder's.
	struct ChangesViewOptions
	{
		bool OffersDiscard = true;
		bool OffersStash = true;

		// "Worktree · <full path>" above the lists; empty draws nothing.
		std::string WhereLine;

		// The commit button's words; unset reads "Commit".
		std::function<std::string()> CommitLabel;

		// The commit button in the warning colour, as for a commit on a detached HEAD.
		bool CommitWarns = false;

		// What the user sees, carried by every write the view asks: a commit's with forCommit set. Unset carries nothing.
		std::function<std::optional<WriteExpectation>(bool forCommit)> Expect;

		// The branch HEAD is on, with its upstream and distance, which Amend asks whether its last commit is pushed. Unset
		// reads the model's own list; set for a model that lists no branches of its own.
		std::function<std::optional<Core::Branch>()> CheckedOutBranch;

		// A status that finds no repository offers to initialise one; otherwise it says the folder is missing.
		bool OffersInit = true;

		// What the empty message box says, Amend off; unset reads "Message".
		std::function<std::string()> MessageHint;
	};

	// The working tree's changes, the commit box and their dialogs. Each host has a view of its own, with its own
	// selection and open folders; what the hosts share is held in ChangesShared.
	class ChangesView
	{
	public:
		// `host` names the host, "panel" or "window", so that each host's questions are its own.
		ChangesView(GitModel& model, ModalGate& modals, EditorBuffers& editor, const ActiveScheme& scheme,
					Core::EventBus& events, ChangesShared& shared, std::string_view host);

		// WARNING: The questions hold pointers into the view's own title strings, so a copy or a move would leave
		// them pointing into the view it was made from.
		ChangesView(const ChangesView&) = delete;
		ChangesView& operator=(const ChangesView&) = delete;
		ChangesView(ChangesView&&) = delete;
		ChangesView& operator=(ChangesView&&) = delete;

		// The reader picked a row: the side panel opens a diff tab, the Git window shows the diff beside the list.
		std::function<void(const OpenDiff& diff)> OnFileChosen;

		void SetOptions(ChangesViewOptions options);

		// The draft the commit box edits from now on.
		void SetShared(ChangesShared& shared);

		// Lets go of what the view holds of the working tree it showed: the selection, the open folders, and an Amend
		// message still on its way. For a host that points the view's model at another working tree.
		// WARNING: Withdraws the view's questions, so it is called outside a popup.
		void Forget();

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

		void RenderWhereLine();

		void RenderCommitBox();

		void RenderStashButton(bool writesBlocked, const Core::WorkingTreeStatus* status);

		// Each write the view asks carries the host's expectation, a commit's with forCommit set.
		std::optional<WriteExpectation> Expectation(bool forCommit) const;
		void RequestStage(std::vector<std::filesystem::path> paths);
		void RequestUnstage(std::vector<std::filesystem::path> paths);
		void RequestCommit(bool amend);

		Core::DiffTarget TargetForSection() const;

		unsigned int ColourOfLetter(char letter, const GitPalette& palette) const;

		void BeginDiscard(const Core::FileStatus& file);

		void RenderDiscardConfirm();

		void RenderStashDialog();

		// The checked-out branch and its upstream when the last commit is already there, which amending it would replace.
		struct AmendPushedQuestion
		{
			std::string Branch;
			std::string Upstream;

			bool operator==(const AmendPushedQuestion&) const = default;
		};

		std::optional<AmendPushedQuestion> PushedLastCommit() const;

		void FillAmendMessage(bool amendToggled);
		void RenderAmendPushedConfirm();

		void ResetStashDialog();

		std::vector<std::filesystem::path> DirtyDocumentsInRepository() const;

		// WARNING: A file drawn inside an open folder never enters m_LastDrawn, which holds top-level
		// rows only. Without this test a change to the top-level list would drop that selection.
		bool SelectedIsInsideExpandedFolder() const;

		GitModel& m_Model;

		EditorBuffers& m_Editor;

		const ActiveScheme& m_Scheme;

		Core::EventBus& m_Events;

		ChangesShared* m_Shared = nullptr;

		ChangesViewOptions m_Options;

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

		// WARNING: Declared before the question that holds a pointer into it, so it is built first.
		const std::string m_DiscardTitle;
		ModalQuestion<DiscardQuestion> m_DiscardQuestion;

		struct StashQuestion
		{
			bool operator==(const StashQuestion&) const = default;
		};

		const std::string m_StashTitle;
		ModalQuestion<StashQuestion> m_StashQuestion;
		std::string m_StashMessage;
		bool m_StashIncludeUntracked = false;

		bool m_StashSaveFailed = false;

		const std::string m_AmendTitle;
		ModalQuestion<AmendPushedQuestion> m_AmendQuestion;

		// The head message held when Amend asked for it, so only a newer answer fills the box; and what was filled, so
		// unticking Amend takes back only a message nobody edited.
		Core::Ref<const std::string> m_AmendMessageAsked;
		bool m_FillingAmendMessage = false;
		std::string m_AmendMessageFilled;
	};
}
