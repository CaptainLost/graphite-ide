#pragma once

#include "Git/GitWorker.h"
#include "Git/RepositoryRequests.h"

#include <memory>

namespace Agent {

	// The "git.*" operations of a WSL repository's blame, line history, branch review and
	// worktrees (stage 3c-1). A question about another worktree comes with that worktree as its
	// root, reached exactly.
	void AddReviewHandlers(GitHandlers& handlers, std::shared_ptr<RepositoryCache> cache);
}
