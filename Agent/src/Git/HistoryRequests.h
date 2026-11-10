#pragma once

#include "Git/GitWorker.h"
#include "Git/RepositoryRequests.h"

#include <memory>

namespace Agent {

	// The "git.*" operations of a WSL repository's history, branches, tags, stashes and checkout
	// (stage 3b), answered on the same handles as its changes.
	void AddHistoryHandlers(GitHandlers& handlers, std::shared_ptr<RepositoryCache> cache);
}
