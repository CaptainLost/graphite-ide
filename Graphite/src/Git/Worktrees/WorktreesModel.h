#pragma once

#include "Core/Base/Base.h"

#include "Git/DeliveryInbox.h"
#include "Git/GenerationSlot.h"
#include "Git/Worktrees/WorktreePort.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Worktree.h"
#include "Core/Jobs/JobSubmitter.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// The worktrees of the open folder's repository, read again for each branch snapshot, and which of them have
	// uncommitted changes, counted while the Git window is on screen. UI thread only, but for the jobs it submits.
	// WARNING: Every job captures `this`; the owner declares this before the queue behind the submitter.
	class WorktreesModel : Core::NonCopyable
	{
	public:
		WorktreesModel(WorktreePort& port, Core::JobSubmitter submit);

		void OnUpdate(Core::Timestep timestep); // drains landed results and asks the counts that are due

		// Asks a read when the snapshot differs from the last asked. The open folder's own worktree is never counted.
		void Follow(const Core::Ref<const std::vector<Core::Branch>>& branches, const std::filesystem::path& openFolder);

		void SetOnScreen(bool onScreen);

		void Clear();

		void SubscribeTo(Core::EventBus& events); // WorkspaceOpenedEvent / WorkspaceClosedEvent -> Clear()

		// The folder of the worktree that has that local branch checked out; empty for none, and for a worktree whose folder is missing.
		std::optional<std::filesystem::path> WorktreeOf(std::string_view branchFullName) const;

		// The same, but empty for the branch checked out in the open folder itself: it has the HEAD dot and its changes are
		// the Local changes row, so it is marked as no worktree.
		std::optional<std::filesystem::path> OtherWorktreeOf(std::string_view branchFullName) const;

		// Empty while unknown, and for the open folder's own worktree.
		std::optional<bool> IsDirty(const std::filesystem::path& worktree) const;

		static constexpr float kPollSeconds = 3.0f;

	private:
		void Land(std::uint64_t generation, Core::Result<std::vector<Core::WorktreeEntry>> result);
		void AskCounts();
		bool IsOpenFolder(const std::filesystem::path& worktree) const;
		bool IsSameFolder(std::uint64_t folder) const;

		WorktreePort& m_Port;
		Core::JobSubmitter m_Submit;

		// WARNING: The inbox's mutex covers the inbox and nothing else; every other member but m_Folder is touched on the
		// UI thread only.
		DeliveryInbox m_Inbox;

		GenerationSlot<std::vector<Core::WorktreeEntry>> m_Entries;
		Core::Ref<const std::vector<Core::Branch>> m_LastAsked;
		std::filesystem::path m_OpenFolder;
		Core::PathKeyText m_OpenFolderKey;

		std::map<std::string, std::filesystem::path, std::less<>> m_WorktreeOfBranch; // by full name
		std::map<Core::PathKeyText, bool> m_Dirty;
		std::set<Core::PathKeyText> m_Listed; // worktrees of the last list: a count of one that left it is dropped
		std::set<Core::PathKeyText> m_Counting;

		bool m_OnScreen = false;
		float m_SinceCount = kPollSeconds; // due at once

		// WARNING: Bumped by Clear; a job of the folder before does nothing, and its answer is dropped.
		std::atomic<std::uint64_t> m_Folder{0};
	};
}
