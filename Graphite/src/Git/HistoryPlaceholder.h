#pragma once

#include "Core/Base/Result.h"

#include <optional>
#include <string>

namespace Graphite {

	// The one definition of "this folder is not a repository", shared by the panels, the
	// history placeholder and the status bar so the three never disagree. Requires that no
	// status was ever delivered: a NotFound after a good status is one failed refresh, and
	// a folder still loading has no answer yet - offering an init then would flash the
	// button up in every real repository for the first refresh.
	bool IsMissingRepository(bool hasStatus, const std::optional<Core::Error>& statusError);

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
