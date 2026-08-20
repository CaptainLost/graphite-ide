#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Commit.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Requests.h"
#include "Core/Git/Status.h"

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
		virtual Core::Result<std::vector<Core::CommitSummary>> QueryLog(
			const Core::LogRequest& request) = 0;
	};
}
