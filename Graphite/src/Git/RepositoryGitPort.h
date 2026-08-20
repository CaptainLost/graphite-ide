#pragma once

#include "Core/Git/Repository.h"
#include "Git/GitPort.h"

#include <filesystem>
#include <mutex>
#include <string>

namespace Graphite {

	// Answers GitModel's questions from a real repository on disk.
	//
	// Each worker thread gets its own libgit2 handle, because a git_repository must not be
	// used from two threads at once. They are cached per thread and per root, so a blame
	// of a large file and a status refresh can run at the same time instead of queueing
	// behind one another. A handle lives until its thread ends; there are a handful of
	// worker threads and one root at a time, so that costs nothing worth reclaiming.
	class RepositoryGitPort final : public GitPort
	{
	public:
		explicit RepositoryGitPort(std::filesystem::path root);

		Core::Result<Core::WorkingTreeStatus> QueryStatus() override;
		Core::Result<std::vector<Core::Branch>> QueryBranches() override;
		Core::Result<std::vector<Core::CommitSummary>> QueryLog(
			const Core::LogRequest& request) override;

		void SetRoot(std::filesystem::path root);

	private:
		Core::Repository* RepositoryForThisThread();

		// Read on worker threads, written on the UI thread when the workspace changes.
		// Stored as a string because that is what the per-thread cache is keyed by.
		std::mutex m_RootMutex;
		std::string m_Root;
	};
}
