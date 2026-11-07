#pragma once

#include "Core/Git/Repository.h"
#include "Git/Blame/HeadBlame.h"
#include "Git/Changes/ChangeBases.h"
#include "Git/Content/ContentPort.h"
#include "Git/GitPort.h"
#include "Git/Lines/LinesPort.h"
#include "Git/Worktrees/WorktreePort.h"
#include "Review/ReviewPort.h"
#include "Settings/UserDirectories.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// WARNING: A git_repository must not be used from two threads at once, so each worker thread
	// caches its own handle per root, and a second one for the content checks, whose objects stay in memory.
	class RepositoryGitPort final : public GitPort, public ChangeBasePort, public BlamePort, public ReviewPort, public LinesPort,
									 public WorktreePort, public ContentPort
	{
	public:
		// Each repository's line store is a folder under lineStoreRoot (LineStoreFor).
		explicit RepositoryGitPort(std::filesystem::path root, std::filesystem::path lineStoreRoot = UserDirectories::GitRepositoriesDirectory());

		Core::Result<Core::WorkingTreeStatus> QueryStatus() override;
		Core::Result<Core::WorkingTreeStatus> QueryUntrackedIn(
			const std::filesystem::path& directory) override;
		Core::Result<std::filesystem::path> GetWorkingDirectory() override;
		Core::Result<std::vector<Core::Branch>> QueryBranches() override;
		Core::Result<std::vector<Core::TipAuthors>> QueryOwnAuthors(const Core::OwnAuthorsRequest& request) override;
		Core::Result<std::optional<std::string>> ReadUserEmail() override;
		Core::Result<std::vector<Core::Remote>> QueryRemotes() override;
		Core::Result<Core::HeadState> QueryHead() override;
		Core::Result<Core::LogPage> QueryLog(const Core::LogRequest& request) override;
		Core::Result<CommitCount> CountCommits(const std::optional<std::string>& ref, Core::FollowedParents parents,
											   const std::optional<CommitCount>& known) override;
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
		Core::Result<bool> StashSave(const std::string& message, Core::UntrackedFiles untrackedFiles) override;
		Core::Result<std::vector<Core::StashEntry>> QueryStashes() override;
		Core::Result<void> StashApply(const Core::Oid& id) override;
		Core::Result<void> StashPop(const Core::Oid& id) override;
		Core::Result<void> StashDrop(const Core::Oid& id) override;
		Core::Result<Core::CheckoutPlan> PlanCheckout(const Core::CheckoutTarget& target) override;
		Core::CheckoutOutcome Checkout(const Core::CheckoutTarget& target) override;
		Core::Result<void> DiscardAll(Core::UntrackedFiles untrackedFiles) override;

		Core::Result<FileTexts> ReadWorkingTreeTexts(
			Core::DiffTarget target,
			const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) override;

		Core::Result<ChangeBases> ReadBases(const std::filesystem::path& absolutePath) override;

		Core::Result<HeadBlame> BlameAtHead(const std::filesystem::path& absolutePath) override;
		Core::Result<Core::LineHistory> QueryLineHistory(const Core::LineHistoryRequest& request) override;

		Core::Result<ReviewRange> ResolveRange(const ReviewKey& key) override;
		Core::Result<Core::Diff> QueryRangeDiff(const RangeEnds& ends, Core::WhitespaceInDiff whitespace) override;
		Core::Result<FileTexts> ReadRangeTexts(const RangeEnds& ends, const std::filesystem::path& oldPath,
											   const std::filesystem::path& newPath) override;
		Core::Result<Core::Diff> QueryWorktreeDiff(const std::filesystem::path& worktree,
												   Core::WhitespaceInDiff whitespace) override;
		Core::Result<FileTexts> ReadWorktreeTexts(const std::filesystem::path& worktree,
												  const std::filesystem::path& oldPath,
												  const std::filesystem::path& newPath) override;
		Core::Result<SinceViewedAnswer> ReadSinceViewed(const SinceViewedRequest& request) override;

		Core::Result<std::vector<Core::WorktreeEntry>> QueryWorktrees() override;
		Core::Result<std::size_t> CountUncommitted(const std::filesystem::path& worktree) override;

		std::uint64_t RepositoryNow() override;
		Core::Result<LineRead> ReadLines(std::uint64_t repository, const LineReadInput& input) override;
		Core::Result<LineRead> RecomputeLines(const LineRead& last, const LineAnswerChange& change, std::int64_t now) override;

		Core::Result<Core::ContentVerdict> CheckContent(std::uint64_t repository, const Core::Oid& branchTip, const Core::Oid& lineTip,
														Core::PatchIdCache& patchIds) override;
		Core::Result<Core::PatchTwinSearch> FindTwin(std::uint64_t repository, const Core::Oid& commit, const Core::Oid& lineTip,
													 std::size_t budget, Core::PatchIdCache& patchIds) override;
		Core::Result<std::vector<std::string>> PathsChangedBetween(std::uint64_t repository, const Core::Oid& older,
																   const Core::Oid& newer) override;
		Core::Result<bool> TreesDifferAt(std::uint64_t repository, const Core::Oid& from, const Core::Oid& to,
										 const std::vector<std::string>& paths) override;

		void SetRoot(std::filesystem::path root);

		// WARNING: Reaches the calling thread's handles only, so it is run on the thread whose handles of a closed or
		// replaced folder must go, without opening the current one.
		void LetGoOfHandlesNoLongerCurrent();

		std::uint64_t GetOpenCount() const { return m_OpenCount.load(); }

		std::size_t GetLiveHandleCountForThisThread() const;

	private:
		Core::Result<Core::Repository*> RepositoryForThisThread();

		// The repository of that generation of the root, or an error once the port has been pointed at a folder since.
		Core::Result<Core::Repository*> RepositoryForThisThreadOf(std::uint64_t generation,
																  Core::ObjectWrites writes = Core::ObjectWrites::OnDisk);

		// The handle that writes objects to disk, or the one whose writes stay in memory, kept apart per thread.
		Core::Result<Core::Repository*> OpenForThisThread(const std::string& root, std::uint64_t generation,
														  Core::ObjectWrites writes = Core::ObjectWrites::OnDisk);

		// WARNING: m_Root and m_Generation are read on worker threads and written on the UI thread,
		// both under m_RootMutex.
		std::mutex m_RootMutex;
		std::string m_Root;
		std::uint64_t m_Generation = 0;

		std::atomic<std::uint64_t> m_OpenCount{0};

		// Set once, read on worker threads.
		const std::filesystem::path m_LineStoreRoot;
	};
}
