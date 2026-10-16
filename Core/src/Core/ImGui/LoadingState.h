#pragma once

#include <optional>

namespace Core {

	void LoadingState(const char* text);

	// One row high, for a list that waits on part of its content.
	void LoadingLine(const char* text);

	// WARNING: The wait starts on the first call, and until a loader is due each call asks for the frame on which it will
	// be, so a short wait shows nothing at all rather than a loader that flashes.
	bool LoaderIsDue(std::optional<double>& waitingSince);
}
