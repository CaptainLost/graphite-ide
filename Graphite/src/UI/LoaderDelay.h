#pragma once

#include <optional>

namespace Graphite {

	// How long a wait goes unmarked, so an answer that comes quickly never flashes a loader.
	inline constexpr double kSecondsBeforeLoaderShows = 0.2;

	// True once the wait has lasted long enough for a loader. The first call starts the wait; until
	// the loader is due, each call asks for the frame on which it will be.
	bool LoaderIsDue(std::optional<double>& waitingSince);
}
