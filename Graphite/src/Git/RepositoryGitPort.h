#pragma once

#include "Core/Git/Repository.h"
#include "Git/Changes/ChangeBases.h"
#include "Git/GitPort.h"
#include "Review/ReviewPort.h"

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
	// caches its own handle per root.
	class RepositoryGitPort final : public GitPort, public ChangeBasePort, public ReviewPort
	{
	public:
		explicit RepositoryGitPort(std::filesystem::path root);

		Core::Result<Core::WorkingTreeStatus> QueryStatus() override;
		Core::Result<Core::WorkingTreeStatus> QueryUntrackedIn(
			const std::filesystem::path& directory) override;
		Core::Result<std::filesystem::path> GetWorkingDirectory() override;
		Core::Result<std::vector<Core::Branch>> QueryBranches() override;
		Core::Result<std::vector<Core::TipAuthors>> QueryOwnAuthors(const Core::OwnAuthorsRequest& request) override;
		Core::Result<std::optional<std::string>> ReadUserEmail() override;
		Core::Result<Core::HeadState> QueryHead() override;
		Core::Result<Core::LogPage> QueryLog(const Core::LogRequest& request) override;
		Core::Result<CommitCount> CountCommits(const std::optional<std::string>& ref,
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

		void SetRoot(std::filesystem::path root);

		std::uint64_t GetOpenCount() const { return m_OpenCount.load(); }

		std::size_t GetLiveHandleCountForThisThread() const;

	private:
		Core::Result<Core::Repository*> RepositoryForThisThread();

		// WARNING: m_Root and m_Generation are read on worker threads and written on the UI thread,
		// both under m_RootMutex.
		std::mutex m_RootMutex;
		std::string m_Root;
		std::uint64_t m_Generation = 0;

		std::atomic<std::uint64_t> m_OpenCount{0};
	};
}
