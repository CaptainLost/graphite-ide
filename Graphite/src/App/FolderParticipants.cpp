#include "App/FolderParticipants.h"

#include "Editor/DocumentWindows.h"
#include "Git/Diff/DiffWindows.h"
#include "Git/GitView.h"
#include "Git/RepositoryGitPort.h"
#include "Language/Server/LanguageServices.h"
#include "Persistence/StateKeeper.h"
#include "Review/ReviewModel.h"
#include "Review/ReviewStore.h"
#include "Review/ReviewWindows.h"
#include "Sessions/SessionWindows.h"
#include "Terminal/TerminalWindows.h"

#include "Core/Text/DocumentSet.h"

namespace Graphite {

	void ServerRootOfFolder::LetGo(FolderExit exit)
	{
		if (exit == FolderExit::Closed)
		{
			m_Services.SetFallbackRoot(std::filesystem::path{});
		}
	}

	void ServerRootOfFolder::Arrive(const FolderArrival& arrival)
	{
		m_Services.SetFallbackRoot(arrival.Root);
	}

	void ServerRootOfFolder::FollowMove(const std::filesystem::path&, const std::filesystem::path& to)
	{
		m_Services.SetFallbackRoot(to);
	}

	void ServerRootOfFolder::Capture(WorkspaceState&) const
	{
	}

	void SavedStateOfFolder::LetGo(FolderExit)
	{
		m_Keeper.CloseWorkspace();
	}

	void SavedStateOfFolder::Arrive(const FolderArrival& arrival)
	{
		m_Keeper.AdoptWorkspace(arrival.Root, arrival.Stored);
	}

	void SavedStateOfFolder::FollowMove(const std::filesystem::path&, const std::filesystem::path& to)
	{
		m_Keeper.MoveWorkspace(to);
	}

	void SavedStateOfFolder::Capture(WorkspaceState&) const
	{
	}

	void DocumentsOfFolder::LetGo(FolderExit exit)
	{
		if (exit == FolderExit::Replaced)
		{
			m_Windows.CarryOverOpenDocuments();
		}
		else
		{
			m_Documents.CloseAll();
		}

		m_Windows.ClearExpectedWrites();
	}

	void DocumentsOfFolder::Arrive(const FolderArrival& arrival)
	{
		m_Windows.RestoreState(arrival.Restorable);
	}

	void DocumentsOfFolder::FollowMove(const std::filesystem::path& from, const std::filesystem::path& to)
	{
		m_Windows.FollowMove(from, to);
	}

	void DocumentsOfFolder::Capture(WorkspaceState& state) const
	{
		WorkspaceState documents = m_Windows.CaptureState();
		state.Tabs = std::move(documents.Tabs);
		state.Active = std::move(documents.Active);
	}

	void SessionsOfFolder::LetGo(FolderExit exit)
	{
		if (exit == FolderExit::Replaced)
		{
			m_Windows.CarryOverOpenSessions();
		}
	}

	void SessionsOfFolder::Arrive(const FolderArrival& arrival)
	{
		m_Windows.RestoreState(arrival.Restorable.Sessions);
	}

	void SessionsOfFolder::FollowMove(const std::filesystem::path&, const std::filesystem::path&)
	{
	}

	void SessionsOfFolder::Capture(WorkspaceState& state) const
	{
		state.Sessions = m_Windows.CaptureState();
	}

	void TerminalsOfFolder::LetGo(FolderExit)
	{
		m_Terminals.CarryOverOpenTerminals();
	}

	void TerminalsOfFolder::Arrive(const FolderArrival& arrival)
	{
		m_Terminals.RestoreState(arrival.Restorable.Terminals, arrival.Trusted);
	}

	// WARNING: A shell keeps its own working directory, and Windows refuses to move a folder a running shell sits
	// in, so there is nothing to follow.
	void TerminalsOfFolder::FollowMove(const std::filesystem::path&, const std::filesystem::path&)
	{
	}

	void TerminalsOfFolder::Capture(WorkspaceState& state) const
	{
		state.Terminals = m_Terminals.CaptureState();
	}

	// WARNING: The Git window clears itself on the folder's own events, so there is nothing more to let go of here.
	void GitWindowOfFolder::LetGo(FolderExit)
	{
	}

	void GitWindowOfFolder::Arrive(const FolderArrival& arrival)
	{
		m_View.RestoreWindowState(arrival.Restorable.GitWindow);
	}

	void GitWindowOfFolder::FollowMove(const std::filesystem::path&, const std::filesystem::path&)
	{
	}

	void GitWindowOfFolder::Capture(WorkspaceState& state) const
	{
		state.GitWindow = m_View.CaptureWindowState();
	}

	void RepositoryOfFolder::LetGo(FolderExit exit)
	{
		// WARNING: Clearing the root releases the port's cached repository handles, which otherwise block the user
		// from deleting .git on Windows.
		if (exit == FolderExit::Closed)
		{
			PointAt(std::filesystem::path{});
		}
	}

	void RepositoryOfFolder::Arrive(const FolderArrival& arrival)
	{
		PointAt(arrival.Root);
	}

	void RepositoryOfFolder::FollowMove(const std::filesystem::path&, const std::filesystem::path& to)
	{
		PointAt(to);
	}

	void RepositoryOfFolder::Capture(WorkspaceState&) const
	{
	}

	void RepositoryOfFolder::PointAt(const std::filesystem::path& root)
	{
		m_Port.SetRoot(root);
		m_LetGoOfOldHandles();
	}

	void DiffsOfFolder::LetGo(FolderExit)
	{
		m_Windows.CloseAll();
	}

	void DiffsOfFolder::Arrive(const FolderArrival& arrival)
	{
		m_Windows.RestoreState(arrival.Restorable.Diffs);
	}

	// WARNING: A diff names its file relative to the repository, which a move of the whole folder leaves as it was.
	void DiffsOfFolder::FollowMove(const std::filesystem::path&, const std::filesystem::path&)
	{
	}

	void DiffsOfFolder::Capture(WorkspaceState& state) const
	{
		state.Diffs = m_Windows.CaptureState();
	}

	void ReviewsOfFolder::LetGo(FolderExit)
	{
		m_Parts.Windows.CloseAll(m_Parts.Model);
		m_Parts.Store.Close();
		m_Parts.ReviewedBranches.reset();
	}

	void ReviewsOfFolder::Arrive(const FolderArrival& arrival)
	{
		m_Parts.Store.Open(arrival.Root);
		m_Parts.Windows.RestoreState(arrival.Restorable.Reviews, m_Parts.Store.GetSessions());
	}

	void ReviewsOfFolder::FollowMove(const std::filesystem::path&, const std::filesystem::path& to)
	{
		m_Parts.Store.MoveTo(to);
	}

	void ReviewsOfFolder::Capture(WorkspaceState& state) const
	{
		state.Reviews = m_Parts.Windows.CaptureState();
	}
}
