#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Requests.h"
#include "Core/Git/Status.h"
#include "Git/FileTexts.h"

#include <filesystem>
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
		virtual Core::Result<std::vector<Core::Branch>> QueryBranches() = 0;
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

		// The writes. Called on a worker thread like everything above, and serialised by
		// GitModel rather than here: this interface answers one call at a time and knows
		// nothing about the others.
		virtual Core::Result<void> Stage(const std::vector<std::filesystem::path>& paths) = 0;
		virtual Core::Result<void> Unstage(const std::vector<std::filesystem::path>& paths) = 0;
		virtual Core::Result<void> DiscardWorkTree(const std::vector<std::filesystem::path>& paths) = 0;
		virtual Core::Result<void> DeleteUntracked(const std::vector<std::filesystem::path>& paths) = 0;
		virtual Core::Result<Core::Oid> Commit(const Core::CommitRequest& request) = 0;
	};
}
