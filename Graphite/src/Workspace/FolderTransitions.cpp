#include "Workspace/FolderTransitions.h"

#include "Persistence/RestrictedRestore.h"
#include "Workspace/WorkspaceTrust.h"

#include <utility>

namespace Graphite {

	FolderTransitions::FolderTransitions(WorkspaceTrust& trust, StateLoader loadState)
		: m_Trust(trust), m_LoadState(std::move(loadState))
	{
	}

	void FolderTransitions::Add(Core::Scope<FolderParticipant> participant)
	{
		m_Participants.push_back(std::move(participant));
	}

	void FolderTransitions::Open(const std::filesystem::path& root)
	{
		// WARNING: A folder opened over another replaces it without a close, so every participant lets go of the last
		// folder here, all of them before any arrives: what one restores must not be let go of by the next.
		for (const Core::Scope<FolderParticipant>& participant : m_Participants)
		{
			participant->LetGo(FolderExit::Replaced);
		}

		FolderArrival arrival;
		arrival.Root = root;
		arrival.Trusted = m_Trust.IsTrusted(root);
		arrival.Stored = m_LoadState(root);
		arrival.Restorable = RestorableState(arrival.Stored, root, arrival.Trusted);

		for (const Core::Scope<FolderParticipant>& participant : m_Participants)
		{
			participant->Arrive(arrival);
		}
	}

	void FolderTransitions::Close()
	{
		for (const Core::Scope<FolderParticipant>& participant : m_Participants)
		{
			participant->LetGo(FolderExit::Closed);
		}
	}

	void FolderTransitions::Move(const std::filesystem::path& from, const std::filesystem::path& to)
	{
		m_Trust.FollowFolderMove(from, to);

		for (const Core::Scope<FolderParticipant>& participant : m_Participants)
		{
			participant->FollowMove(from, to);
		}
	}

	WorkspaceState FolderTransitions::Capture() const
	{
		WorkspaceState state;

		for (const Core::Scope<FolderParticipant>& participant : m_Participants)
		{
			participant->Capture(state);
		}

		return state;
	}
}
