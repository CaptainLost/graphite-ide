#pragma once

#include "Git/GitWorker.h"

namespace Agent {

	// The agent's answers to the "git.*" operations of a WSL repository (stage 3a: its changes).
	GitHandlers DefaultGitHandlers();

	// The questions the Explorer asks while it draws a folder ("git.ignoreSetup", "git.ignored"),
	// answered on a lane of their own with handles of their own, so they never wait behind a
	// status of a huge tree.
	GitHandlers DefaultQuickGitHandlers();
}
