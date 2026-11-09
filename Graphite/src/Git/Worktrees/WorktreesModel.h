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
#include <chrono>
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
		WorktreesModel(WorktreePort& port, Core::JobSubmitter submit, std::function<std::int64_t()> nowSeconds);

		void OnUpdate(Core::Timestep timestep); // drains landed results and asks the counts that are due

		// Asks a read when the snapshot differs from the last asked. The open folder's own worktree is never counted.
		void Follow(const Core::Ref<const std::vector<Core::Branch>>& branches, const std::filesystem::path& openFolder);

		void SetOnScreen(bool onScreen);

		void Clear();

		void SubscribeTo(Core::EventBus& events); // WorkspaceOpenedEvent / WorkspaceClosedEvent -> Clear()

		// Every worktree of the repository, detached and folder-missing ones included; null while unread.
		const std::vector<Core::WorktreeEntry>* Entries() const;

		// Changes whenever Entries() answers with another list, or with a failure.
		std::uint64_t EntriesGeneration() const { return m_Entries.GetApplied(); }

		// Why the last read of the list failed; empty when it did not. A failed first read leaves Entries() null.
		const std::optional<Core::Error>& ListError() const { return m_Entries.GetError(); }

		// Whether the list's first read has answered, with the list or with a failure.
		bool HasListAnswered() const { return Entries() != nullptr || ListError().has_value(); }

		// The number of files with changes; empty while unknown, after a failed count, and for the open folder's own worktree.
		std::optional<std::size_t> CountOf(const std::filesystem::path& worktree) const;

		// The newest of the worktree's reflog, index and HEAD commit times and of its newest changed file; empty while unknown.
		std::optional<std::chrono::sys_seconds> LastActivity(const std::filesystem::path& worktree) const;

		// Whether the last activity is no older than kLiveSeconds.
		bool IsLive(const std::filesystem::path& worktree) const;

		// Whether every worktree has been counted once, or kFirstCountCapSeconds on screen passed since the folder's first
		// list landed. Once true it stays true for the folder: a worktree added later, or a list read again, holds nothing.
		bool IsFirstCountSettled() const { return m_FirstCountSettled; }

		// The clock the model tells live worktrees by, in seconds since the epoch.
		std::int64_t NowSeconds() const { return m_NowSeconds(); }

		// The worktree the user is looking at: never polled nor swept, since its watcher keeps it fresh.
		void SetLookedAt(std::optional<std::filesystem::path> worktree);

		// Counts the worktree and reads its activity once, outside the polling, as its watch says it changed. Nothing is
		// asked while a count of it is on its way, nor for the open folder, a missing folder or a worktree not listed.
		void RecountNow(const std::filesystem::path& worktree);

		// The folder of the worktree that has that local branch checked out; empty for none, and for a worktree whose folder is missing.
		std::optional<std::filesystem::path> WorktreeOf(std::string_view branchFullName) const;

		// The same, but empty for the branch checked out in the open folder itself: it has the HEAD dot and its changes are
		// the Local changes row, so it is marked as no worktree.
		std::optional<std::filesystem::path> OtherWorktreeOf(std::string_view branchFullName) const;

		// Empty while unknown, after a failed count, and for the open folder's own worktree.
		std::optional<bool> IsDirty(const std::filesystem::path& worktree) const;

		// The same, for a row drawn this frame: only the worktrees of rows in view are counted.
		std::optional<bool> DirtyInView(const std::filesystem::path& worktree);

		// Why the last count of the worktree's changes failed; empty when it did not.
		std::optional<std::string> CountFailureOf(const std::filesystem::path& worktree) const;

		// A worktree is counted again this long after its last count landed, twice as long each time the count comes back
		// the same, up to the longest; a count that changed starts it over.
		static constexpr float kPollSeconds = 3.0f;
		static constexpr float kLongestPollSeconds = 48.0f;

		// The first count of every worktree holds the list this long at most.
		static constexpr float kFirstCountCapSeconds = 3.0f;

		// A worktree out of view is counted again at most this often, one at a time, the longest-uncounted first.
		static constexpr float kSweepSeconds = 10.0f;

		// A worktree whose last activity is this recent is live.
		static constexpr std::int64_t kLiveSeconds = 60;

	private:
		struct CountTiming
		{
			float Due = 0.0f; // on m_Clock
			float Interval = kPollSeconds;
			std::optional<std::size_t> Last;
		};

		void Land(std::uint64_t generation, Core::Result<std::vector<Core::WorktreeEntry>> result);
		struct Observed
		{
			bool Counted = false; // a count, failed or not, has landed
			float LandedAt = 0.0f; // on m_Clock
			std::optional<std::size_t> Count;
			std::optional<std::chrono::sys_seconds> Activity;
			std::optional<std::chrono::sys_seconds> NewestChange;
		};

		void AskCounts(const std::set<Core::PathKeyText>& inView);
		void AskSweep(const std::set<Core::PathKeyText>& inView);
		void AskCount(const Core::PathKeyText& key, const std::filesystem::path& path);
		void LandCount(const Core::PathKeyText& key, const Core::Result<WorktreeActivity>& activity, const Core::Result<WorktreeCount>& count);
		bool IsCountable(const Core::WorktreeEntry& entry) const;
		void SettleFirstCountWhenDone();
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
		std::map<Core::PathKeyText, std::string> m_Failures;
		std::map<Core::PathKeyText, CountTiming> m_Timing;
		std::map<Core::PathKeyText, Observed> m_Observed;
		std::set<Core::PathKeyText> m_Listed; // worktrees of the last list: a count of one that left it is dropped
		std::set<Core::PathKeyText> m_Counting;

		// The worktrees whose rows were drawn since the last update.
		std::set<Core::PathKeyText> m_InView;

		bool m_OnScreen = false;
		float m_Clock = 0.0f; // seconds on screen
		float m_NextSweep = kSweepSeconds; // on m_Clock
		float m_FirstCountWaited = 0.0f; // seconds on screen since the folder's first list landed
		bool m_FirstCountSettled = false;
		Core::PathKeyText m_LookedAt;
		std::function<std::int64_t()> m_NowSeconds;

		// WARNING: Bumped by Clear; a job of the folder before does nothing, and its answer is dropped.
		std::atomic<std::uint64_t> m_Folder{0};
	};
}
