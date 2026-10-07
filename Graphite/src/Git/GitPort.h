#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Checkout.h"
#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Head.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Requests.h"
#include "Core/Git/Stash.h"
#include "Core/Git/Status.h"
#include "Git/Diff/FileTexts.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Graphite {

	// What GitModel needs a repository for, and nothing else. Implemented for real by
	// opening a Core::Repository on the calling thread; implemented in the tests by a
	// class that answers from memory, which is why those tests need neither libgit2 nor a
	// directory on disk.
	//
	// Every method is called on a worker thread.
	class GitPort
	{
	public:
		virtual ~GitPort() = default;

		virtual Core::Result<Core::WorkingTreeStatus> QueryStatus() = 0;

		// Every untracked file under one directory. Asked only when a reader opens that
		// directory up; see Core::Repository::QueryUntrackedIn for why it is not part of
		// the status itself.
		virtual Core::Result<Core::WorkingTreeStatus> QueryUntrackedIn(
			const std::filesystem::path& directory) = 0;

		// The directory libgit2 actually resolved against, which is not always the folder the
		// user opened: Core::Repository::Open searches upward for a .git, so opening a
		// subdirectory of a repository yields a working directory above it. Every path a
		// status query names is relative to this, never to the folder that was opened -
		// callers that need to turn one of those paths into an absolute one must join it here.
		virtual Core::Result<std::filesystem::path> GetWorkingDirectory() = 0;

		virtual Core::Result<std::vector<Core::Branch>> QueryBranches() = 0;
		virtual Core::Result<Core::HeadState> QueryHead() = 0;
		virtual Core::Result<Core::LogPage> QueryLog(const Core::LogRequest& request) = 0;
		virtual Core::Result<Core::Diff> QueryDiff(const Core::DiffRequest& request) = 0;

		// Both sides of one file as they stood around a commit: the new side from that
		// commit, the old side from its first parent. Both paths are taken because a
		// renamed file has a different name on each side, and the diff calling this
		// already knows both - working it out again here would put a second copy of that
		// knowledge somewhere it does not belong.
		virtual Core::Result<FileTexts> ReadFileTexts(
			const Core::Oid& commit,
			const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) = 0;

		// Both sides of one file's text for a working-tree diff. Which two sources those
		// sides come from depends on the target - see SidesFor - and a commit is not one of
		// them, which is why ReadFileTexts above cannot answer this.
		//
		// A side that is absent comes back with its Has flag false rather than as an error:
		// an untracked file has no older side, a deleted one has no newer side, and both are
		// answers.
		virtual Core::Result<FileTexts> ReadWorkingTreeTexts(
			Core::DiffTarget target,
			const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) = 0;

		// The writes. Called on a worker thread like everything above, and serialised by
		// GitModel rather than here: this interface answers one call at a time and knows
		// nothing about the others.
		virtual Core::Result<void> Stage(const std::vector<std::filesystem::path>& paths) = 0;
		virtual Core::Result<void> Unstage(const std::vector<std::filesystem::path>& paths) = 0;
		virtual Core::Result<void> DiscardWorkTree(const std::vector<std::filesystem::path>& paths) = 0;
		virtual Core::Result<void> DeleteUntracked(const std::vector<std::filesystem::path>& paths) = 0;
		virtual Core::Result<Core::Oid> Commit(const Core::CommitRequest& request) = 0;

		// Branches. A delete is the caller's decision to make after CountUnmergedCommits has
		// said what would be lost; this interface does not second-guess it.
		virtual Core::Result<void> CreateBranch(const std::string& name, const Core::Oid& from) = 0;
		virtual Core::Result<void> RenameBranch(const std::string& from, const std::string& to) = 0;
		virtual Core::Result<std::size_t> CountUnmergedCommits(const std::string& name) = 0;
		virtual Core::Result<void> DeleteBranch(const std::string& name) = 0;

		// Stashes. The writes take the stash commit's id rather than its index, because
		// stash@{N} shifts whenever another stash is pushed or dropped. StashSave answers
		// false when there was nothing to stash.
		virtual Core::Result<bool> StashSave(const std::string& message, Core::UntrackedFiles untrackedFiles) = 0;
		virtual Core::Result<std::vector<Core::StashEntry>> QueryStashes() = 0;
		virtual Core::Result<void> StashApply(const Core::Oid& id) = 0;
		virtual Core::Result<void> StashPop(const Core::Oid& id) = 0;
		virtual Core::Result<void> StashDrop(const Core::Oid& id) = 0;

		// A dry run that changes nothing, then the checkout itself. Checkout reports through
		// an outcome rather than a Result because a failure partway through still has to name
		// the paths that may have changed.
		virtual Core::Result<Core::CheckoutPlan> PlanCheckout(const Core::CheckoutTarget& target) = 0;
		virtual Core::CheckoutOutcome Checkout(const Core::CheckoutTarget& target) = 0;
		virtual Core::Result<void> DiscardAll(Core::UntrackedFiles untrackedFiles) = 0;

		// Creates a repository at the folder this port was given. NotFound when no folder is
		// open.
		virtual Core::Result<void> Init() = 0;
	};
}
