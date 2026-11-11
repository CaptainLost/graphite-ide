#pragma once

#include "Git/GitWorker.h"

namespace Agent {

	// Listing and searching a WSL folder in the distro, not file by file over the share (stage 4):
	// "files.list" (relative paths, NUL-separated in the payload) and "search.text" (hits as JSON in
	// the payload, counts in the result). Both walk the folder without .git, and without what git
	// ignores when asked to; files are read as the host's search reads them (ReadDiskText).
	GitHandlers SearchHandlers();
}
