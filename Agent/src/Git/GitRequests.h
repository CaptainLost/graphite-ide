#pragma once

#include "Git/GitWorker.h"

namespace Agent {

	// The agent's answers to the "git.*" operations of a WSL repository (stage 3a: its changes).
	GitHandlers DefaultGitHandlers();
}
