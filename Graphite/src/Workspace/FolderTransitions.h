#pragma once

#include "Persistence/PersistedState.h"

#include "Core/Base/Base.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace Graphite {

	class WorkspaceTrust;

	enum class FolderExit : std::uint8_t
	{
		Replaced,
		Closed
	};

	struct FolderArrival
	{
		std::filesystem::path Root;

		// As it was saved: what the state keeper compares the next write against.
		WorkspaceState Stored;

		// What may be opened again: an untrusted folder keeps only what lies inside it.
		WorkspaceState Restorable;

		bool Trusted = false;
	};

	// Something the application holds for the open folder. Every step is pure so that each participant decides
	// it: a step left out by default is how the open folder's move once missed the git port.
	class FolderParticipant
	{
	public:
		virtual ~FolderParticipant() = default;

		// The open folder is going: another takes its place, or none does.
		virtual void LetGo(FolderExit exit) = 0;

		virtual void Arrive(const FolderArrival& arrival) = 0;

		// The open folder was renamed or moved on disk while open.
		virtual void FollowMove(const std::filesystem::path& from, const std::filesystem::path& to) = 0;

		// Writes this participant's part of the state saved for the open folder.
		virtual void Capture(WorkspaceState& state) const = 0;
	};

	// Opens, closes and moves the folder through every participant, in the order they were added: on opening, all
	// let go of the last folder before any arrives in the new one. The folder's trust moves with it before anyone
	// follows a move, since the participants that follow ask it.
	class FolderTransitions
	{
	public:
		using StateLoader = std::function<WorkspaceState(const std::filesystem::path& root)>;

		FolderTransitions(WorkspaceTrust& trust, StateLoader loadState);

		void Add(Core::Scope<FolderParticipant> participant);

		void Open(const std::filesystem::path& root);

		void Close();

		void Move(const std::filesystem::path& from, const std::filesystem::path& to);

		WorkspaceState Capture() const;

	private:
		WorkspaceTrust& m_Trust;
		StateLoader m_LoadState;

		std::vector<Core::Scope<FolderParticipant>> m_Participants;
	};
}
