#pragma once

namespace Core {

	// WARNING: Each loader waits a fifth of a second before it shows, so a short wait shows nothing at all rather than a
	// loader that flashes. The wait is kept per loader in the window, so the caller keeps no state for it: a frame that
	// does not draw the loader ends its wait, and the next one starts a new one.
	void LoadingState(const char* text);

	// One row high, for a list that waits on part of its content. The row is taken while the loader waits too, so the
	// list does not move when it shows.
	void LoadingLine(const char* text);
}