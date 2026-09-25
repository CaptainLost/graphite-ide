#pragma once

#include <filesystem>
#include <vector>

namespace Graphite {

	// Whether every directory in a watcher batch lies inside `<root>/.graphite`, the directory the
	// IDE keeps its own per-folder state and analysis records in.
	//
	// Such a batch is the IDE hearing itself write: the session state is rewritten every couple
	// of seconds while the caret moves, and announcing that as a change to the folder's files
	// would have every subscriber — git status above all — go and look again for nothing. An
	// empty batch answers false: there is nothing in it to call the IDE's own.
	bool OnlyOwnStateChanged(const std::vector<std::filesystem::path>& directories,
							 const std::filesystem::path& root);
}
