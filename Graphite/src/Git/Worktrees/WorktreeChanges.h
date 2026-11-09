#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Git/Changes/ChangesShared.h"
#include "Git/Debouncer.h"
#include "Git/GitModel.h"
#include "Git/GitPort.h"
#include "Git/Worktrees/WriteExpectation.h"
#include "Review/WorktreeWatch.h"

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>

namespace Graphite {

	// The uncommitted changes of the worktree the Git window looks at, when that is another worktree than the open folder:
	// a GitModel of its own over a port pointed at that worktree, a watch on it while it is on screen, and a commit draft
	// for each worktree looked at in the session. The model is bound to the worktree, never to the workspace's events.
	// UI thread only, but for the jobs its model submits.
	// WARNING: Every job the model submits captures it; the owner declares this before the queues behind both submitters.
	class WorktreeChanges : Core::NonCopyable
	{
	public:
		// setPortRoot points `port` at a worktree, or at nothing for an empty path, and lets go of what the port held of
		// the one before.
		WorktreeChanges(GitPort& port, std::function<void(const std::filesystem::path&)> setPortRoot,
						Core::JobSubmitter read, Core::JobSubmitter write, WorktreeWatchFactory watch);

		// Asked every frame with the worktree whose changes row is shown, empty for none. Re-roots the model and the port
		// when it names another worktree, and watches it only while on screen: off screen nothing is watched or read.
		// WARNING: A write asked of the model, and the ones queued behind it, are let finish first, so none runs in the
		// worktree followed next; until then Worktree() still names the one before.
		void Follow(std::optional<std::filesystem::path> worktree, bool onScreen);

		// Applies what the model's jobs finished; a watch change asks the status and HEAD again after a debounce.
		void OnUpdate(Core::Timestep timestep);

		// Told the watched worktree each time its watch's debounce fires, after the model has been asked to read it again.
		std::function<void(const std::filesystem::path& worktree)> OnWatchFired;

		// The model's write failures, taken as each update lands them.
		// WARNING: Taken from here, never from Model(): following another worktree clears the model, which would drop a
		// failure of the write the switch waited for before anyone had read it.
		std::optional<GitModel::WriteFailure> TakeWriteError();

		GitModel& Model() { return m_Model; }
		const GitModel& Model() const { return m_Model; }

		// The worktree the model reads; empty for none.
		const std::optional<std::filesystem::path>& Worktree() const { return m_Worktree; }

		// The "live" marker: the worktree is being watched.
		bool IsWatching() const { return m_FolderWatch != nullptr; }

		// The worktree kept fresh by the watch, which the worktree counts need not poll; empty while nothing is watched.
		std::optional<std::filesystem::path> LookedAt() const;

		// The followed worktree's own draft, kept per worktree for the session; a scratch draft while none is followed.
		// A commit that lands clears it.
		ChangesShared& Draft();

		// What the user sees of the worktree, for a write to carry: the HEAD and the status the model holds. A commit's
		// carries the staged paths too.
		WriteExpectation ExpectationNow(bool forCommit) const;

	private:
		void Settle();

		void Reroot();

		void Watch(bool lookedAt);

		std::function<void(const std::filesystem::path&)> m_SetPortRoot;
		WorktreeWatchFactory m_StartWatch;

		GitModel m_Model;

		std::optional<std::filesystem::path> m_Wanted;
		std::optional<std::filesystem::path> m_Worktree;
		bool m_OnScreen = false;
		bool m_LookedAt = false;

		Core::Scope<WorktreeWatch> m_FolderWatch;

		// A linked worktree's index and HEAD live in its git directory, outside the folder: a stage or a commit made by
		// another process there touches no file the folder's watch sees.
		Core::Scope<WorktreeWatch> m_GitDirectoryWatch;

		Debouncer m_Debounce;

		std::deque<GitModel::WriteFailure> m_WriteErrors;

		std::map<Core::PathKeyText, ChangesShared> m_Drafts;
		ChangesShared m_NoDraft;
	};

	// The git directory a linked worktree's `.git` file names; empty when `.git` is a directory or cannot be read.
	std::optional<std::filesystem::path> LinkedGitDirectoryOf(const std::filesystem::path& worktree);
}
