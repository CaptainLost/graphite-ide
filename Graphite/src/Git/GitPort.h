#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Checkout.h"
#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Head.h"
#include "Core/Git/OwnAuthors.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Remote.h"
#include "Core/Git/Requests.h"
#include "Core/Git/Stash.h"
#include "Core/Git/Status.h"
#include "Core/Git/Tag.h"
#include "Git/Diff/FileTexts.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// A history's length with the tip it was counted from: the same tip always counts the same.
	struct CommitCount
	{
		Core::Oid Tip;
		std::size_t Count = 0;

		friend bool operator==(const CommitCount&, const CommitCount&) = default;
	};

	// WARNING: Every method is called on a worker thread.
	class GitPort
	{
	public:
		virtual ~GitPort() = default;

		virtual Core::Result<Core::WorkingTreeStatus> QueryStatus() = 0;

		virtual Core::Result<Core::WorkingTreeStatus> QueryUntrackedIn(
			const std::filesystem::path& directory) = 0;

		// WARNING: Status paths are relative to this directory, which can sit above the opened
		// folder because Core::Repository::Open searches upward for a .git.
		virtual Core::Result<std::filesystem::path> GetWorkingDirectory() = 0;

		virtual Core::Result<std::vector<Core::Branch>> QueryBranches() = 0;

		// WARNING: The cache is shared by the jobs of one folder and is safe to use from any thread; a tag object it
		// already holds is not read again.
		virtual Core::Result<std::vector<Core::Tag>> QueryTags(Core::TagCache& cache) = 0;

		virtual Core::Result<std::vector<Core::TipAuthors>> QueryOwnAuthors(const Core::OwnAuthorsRequest& request) = 0;
		virtual Core::Result<std::optional<std::string>> ReadUserEmail() = 0;
		virtual Core::Result<std::vector<Core::Remote>> QueryRemotes() = 0;
		virtual Core::Result<Core::HeadState> QueryHead() = 0;
		virtual Core::Result<Core::LogPage> QueryLog(const Core::LogRequest& request) = 0;

		// WARNING: Answers `known` unchanged, without walking, while the ref still points at the tip
		// it was counted from; `known` must have been counted along the same parents. The model asks
		// on every refresh, and most refreshes are a file saved, not a commit made. Without a ref,
		// HEAD's history.
		virtual Core::Result<CommitCount> CountCommits(const std::optional<std::string>& ref, Core::FollowedParents parents,
													   const std::optional<CommitCount>& known) = 0;
		virtual Core::Result<Core::Diff> QueryDiff(const Core::DiffRequest& request) = 0;

		virtual Core::Result<FileTexts> ReadFileTexts(
			const Core::Oid& commit,
			const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) = 0;

		// WARNING: A side that is absent comes back with its Has flag false, not as an error:
		// an untracked file has no older side and a deleted one has no newer side.
		virtual Core::Result<FileTexts> ReadWorkingTreeTexts(
			Core::DiffTarget target,
			const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) = 0;

		virtual Core::Result<void> Stage(const std::vector<std::filesystem::path>& paths) = 0;
		virtual Core::Result<void> Unstage(const std::vector<std::filesystem::path>& paths) = 0;
		virtual Core::Result<void> DiscardWorkTree(const std::vector<std::filesystem::path>& paths) = 0;
		virtual Core::Result<void> DeleteUntracked(const std::vector<std::filesystem::path>& paths) = 0;
		virtual Core::Result<Core::Oid> Commit(const Core::CommitRequest& request) = 0;

		virtual Core::Result<void> CreateBranch(const std::string& name, const Core::Oid& from) = 0;
		virtual Core::Result<void> RenameBranch(const std::string& from, const std::string& to) = 0;
		virtual Core::Result<std::size_t> CountUnmergedCommits(const std::string& name) = 0;
		virtual Core::Result<void> DeleteBranch(const std::string& name) = 0;

		// WARNING: StashSave answers false when there was nothing to stash. The other stash
		// writes take the stash commit's id because stash@{N} shifts as stashes are pushed or dropped.
		virtual Core::Result<bool> StashSave(const std::string& message, Core::UntrackedFiles untrackedFiles) = 0;
		virtual Core::Result<std::vector<Core::StashEntry>> QueryStashes() = 0;
		virtual Core::Result<void> StashApply(const Core::Oid& id) = 0;
		virtual Core::Result<void> StashPop(const Core::Oid& id) = 0;
		virtual Core::Result<void> StashDrop(const Core::Oid& id) = 0;

		virtual Core::Result<Core::CheckoutPlan> PlanCheckout(const Core::CheckoutTarget& target) = 0;
		virtual Core::CheckoutOutcome Checkout(const Core::CheckoutTarget& target) = 0;
		virtual Core::Result<void> DiscardAll(Core::UntrackedFiles untrackedFiles) = 0;

		// WARNING: Init answers NotFound when no folder is open.
		virtual Core::Result<void> Init() = 0;
	};
}
