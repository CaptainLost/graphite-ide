#pragma once

#include "Git/GitWorker.h"

namespace Agent {

	// The "git.*" content checks of a WSL repository (stage 3c-2): whether a branch is merged by
	// its contents, a commit's patch twin, the paths two commits differ in. Answered on handles of
	// their own opened with ObjectWrites::InMemory, so a merge check never writes into the
	// repository, with each repository's patch ids kept for the agent's life. The session runs them on a lane of their
	// own (AgentSession::m_ContentWorker), and they touch nothing the other handlers use.
	void AddContentHandlers(GitHandlers& handlers);
}
