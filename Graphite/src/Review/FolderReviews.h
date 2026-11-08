#pragma once

#include "Git/Lines/HeldLines.h"
#include "Persistence/PersistedState.h"
#include "Review/ReviewMenu.h"
#include "Review/ReviewStore.h"
#include "Review/ReviewWindows.h"

#include "Core/Base/Base.h"
#include "Core/Base/Timestep.h"
#include "Core/Git/Ref.h"

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Graphite {

	class GitModel;
	class LinesModel;
	class ModalGate;
	class ReviewModel;

	// How the review tabs are drawn this frame: the theme's colours, the diff settings and where new tabs dock.
	struct ReviewLook
	{
		ImVec4 Danger;
		const GitPalette& Palette;
		const SyntaxPalette& Syntax;
		ImU32 Selection = 0;
		DiffViewOptions DiffOptions;
		unsigned int CentreNode = 0;
		Core::TabCloseRequests& CloseRequests;
	};

	// The branch reviews of the open folder: their sessions and the file that keeps them, their tabs, and the branch
	// listing and line read they follow. The Git window asks it for a branch's review menu and hands it what was picked.
	class FolderReviews
	{
	public:
		FolderReviews(GitModel& git, LinesModel& lines, ReviewModel& model, ModalGate& modals);

		// Follows a new branch listing — a branch renamed takes its session with it, a session of a branch gone is
		// pruned — and a new line read; then lands what the model finished, and saves the sessions once a save is due.
		void Update(Core::Timestep timestep);

		// Empty while the branches are not listed.
		ReviewMenu MenuOf(const Core::Branch& branch) const;

		void Apply(const ReviewMenuItem& item);

		// Opens the branch's tab, opening a draft against its base first when it has no session; nothing while its base
		// cannot be told.
		void Open(const std::string& branch);

		// Graphite itself renamed the branch: its tab follows the rename once the listing shows it.
		void NoteRename(std::string from, std::string to);

		void Render(const ReviewLook& look);

		// The folder's transitions: tabs and sessions go with the folder, and come back with the next.
		void LetGo();
		void Arrive(const std::filesystem::path& root, const std::vector<ReviewTabState>& restorable);
		void FollowMove(const std::filesystem::path& to);
		std::vector<ReviewTabState> Capture() const;

		// Saves what changed now, as the application closes.
		void Flush();

		// The tabs' callbacks, which the layer sets once.
		ReviewWindows& Windows() { return m_Windows; }

	private:
		void FollowBranches();

		// Opens a draft against the branch's base when it has no session; false while its base cannot be told.
		bool OpenDraft(const std::string& branch);

		GitModel& m_Git;
		LinesModel& m_Lines;
		ReviewModel& m_Model;

		ReviewWindows m_Windows;
		ReviewStore m_Store;

		// The listing last followed: GitModel replaces its snapshot only with a list it read whole.
		Core::Ref<const std::vector<Core::Branch>> m_FollowedBranches;
		HeldLines m_ReadLines;
	};
}
