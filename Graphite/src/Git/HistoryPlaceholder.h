#pragma once

#include "Core/Base/Result.h"

#include <optional>
#include <string>

namespace Graphite {

	// What the history column says while it has no page of commits to draw. Three different
	// situations look the same from there - an answer still on its way, a folder that is not a
	// repository, and a query that failed - and each needs saying differently, or a failure reads
	// as a wait that never ends.
	//
	// A status that failed with NotFound, and no status ever delivered, is what a folder with no
	// repository looks like: the port answers every query that way when it has nothing to open.
	std::string HistoryPlaceholder(bool hasStatus, const std::optional<Core::Error>& statusError,
								   const std::optional<Core::Error>& logError);
}
