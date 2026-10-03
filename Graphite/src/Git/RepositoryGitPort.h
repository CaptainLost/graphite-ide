#pragma once

#include "Core/Git/Repository.h"
#include "Git/Changes/ChangeBases.h"
#include "Git/GitPort.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace Graphite {

	// Answers both GitModel's and ChangeMarkersModel's questions from a real repository on disk.
	//
	// Each worker thread gets its own libgit2 handle, because a git_repository must not be
	// used from two threads at once. They are cached per thread and per root, so a blame
	// of a large file and a status refresh can run at the same time instead of queueing
	// behind one another. A handle lives until its thread ends or its root is superseded;
	// there are a handful of worker threads and one root at a time, so that costs nothing
	// worth reclaiming beyond what SetRoot already invalidates.
	class RepositoryGitPort final : public GitPort, public ChangeBasePort
	{
	public:
		explicit RepositoryGitPort(std::filesystem::path root);

		Core::Result<Core::WorkingTreeStatus> QueryStatus() override;
		Core::Result<Core::WorkingTreeStatus> QueryUntrackedIn(
			const std::filesystem::path& directory) override;
		Core::Result<std::filesystem::path> GetWorkingDirectory() override;
		Core::Result<std::vector<Core::Branch>> QueryBranches() override;
		Core::Result<Core::HeadState> QueryHead() override;
		Core::Result<Core::LogPage> QueryLog(const Core::LogRequest& request) override;
		Core::Result<Core::Diff> QueryDiff(const Core::DiffRequest& request) override;
		Core::Result<FileTexts> ReadFileTexts(
			const Core::Oid& commit,
			const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) override;

		Core::Result<void> Stage(const std::vector<std::filesystem::path>& paths) override;
		Core::Result<void> Unstage(const std::vector<std::filesystem::path>& paths) override;
		Core::Result<void> DiscardWorkTree(const std::vector<std::filesystem::path>& paths) override;
		Core::Result<void> DeleteUntracked(const std::vector<std::filesystem::path>& paths) override;
		Core::Result<Core::Oid> Commit(const Core::CommitRequest& request) override;
		Core::Result<void> Init() override;

		Core::Result<void> CreateBranch(const std::string& name, const Core::Oid& from) override;
		Core::Result<void> RenameBranch(const std::string& from, const std::string& to) override;
		Core::Result<std::size_t> CountUnmergedCommits(const std::string& name) override;
		Core::Result<void> DeleteBranch(const std::string& name) override;
		Core::Result<bool> StashSave(const std::string& message, bool includeUntracked) override;
		Core::Result<std::vector<Core::StashEntry>> QueryStashes() override;
		Core::Result<void> StashApply(const Core::Oid& id) override;
		Core::Result<void> StashPop(const Core::Oid& id) override;
		Core::Result<void> StashDrop(const Core::Oid& id) override;
		Core::Result<Core::CheckoutPlan> PlanCheckout(const Core::CheckoutTarget& target) override;
		Core::CheckoutOutcome Checkout(const Core::CheckoutTarget& target) override;
		Core::Result<void> DiscardAll(bool includeUntracked) override;

		Core::Result<FileTexts> ReadWorkingTreeTexts(
			Core::DiffTarget target,
			const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) override;

		// ChangeBasePort. Uses the same per-thread handle as every query above.
		Core::Result<ChangeBases> ReadBases(const std::filesystem::path& absolutePath) override;

		void SetRoot(std::filesystem::path root);

		// How many times this instance has opened a git_repository handle, across every
		// worker thread. Exists for tests: the thread-local cache is otherwise invisible
		// from outside the thread that owns it, so a filesystem assertion after a close
		// cannot tell "the handle was released" from "the handle was never touched again" -
		// counting opens can, because a stale-then-reopened root shows up as two opens
		// where a silently reused handle would show up as one.
		std::uint64_t GetOpenCount() const { return m_OpenCount.load(); }

		// How many handles this thread currently has cached, for this port's root or any
		// other. Where GetOpenCount only ever grows, this can shrink: a switch from one
		// root to another has to leave the old root's entry evicted, not merely open a
		// second one for the new root, and an open count cannot tell "evicted" from "left
		// behind" - both look the same from the outside once something has been opened for
		// the new root. This can, because a leaked entry keeps the count up. Must be
		// called from the thread whose cache is in question.
		std::size_t GetLiveHandleCountForThisThread() const;

	private:
		// The handle for the current root on the calling thread, or Open's own error: a
		// NotFound only when there is genuinely no repository, so callers can tell "none"
		// from "one that would not open".
		Core::Result<Core::Repository*> RepositoryForThisThread();

		// Read on worker threads, written on the UI thread when the workspace changes.
		// Stored as a string because that is what the per-thread cache is keyed by.
		//
		// m_Generation is bumped on every SetRoot, including a call that repeats the same
		// path: reopening the same root - the user deleted .git and ran git init again, or
		// re-cloned in place - must not hand a query the old, now-dead handle. A cached
		// handle whose generation does not match the current one is stale even though its
		// key still matches, and is dropped rather than reused.
		std::mutex m_RootMutex;
		std::string m_Root;
		std::uint64_t m_Generation = 0;

		// Incremented from whichever worker thread just opened a handle, so it has to be
		// atomic even though every other member here is only ever touched under
		// m_RootMutex or from the UI thread.
		std::atomic<std::uint64_t> m_OpenCount{0};
	};
}
