#pragma once

#include <cstdint>

namespace Core {

	enum class WhileLoaderWaits : std::uint8_t
	{
		ShowNothing,
		ShowText,
		TakeNoRoom
	};

	// WARNING: Each loader waits a fifth of a second before it shows, so a short wait shows nothing at all rather than a
	// loader that flashes. The wait is kept per loader in the window, so the caller keeps no state for it: a frame that
	// does not draw the loader ends its wait, and the next one starts a new one.
	void LoadingState(const char* text);

	// One row high, for a list that waits on part of its content. The row is taken while the loader waits too, so the
	// list does not move when it shows. A line over content already on screen can take no room while it waits instead,
	// so a wait that ends within a frame or two does not push that content down and back.
	void LoadingLine(const char* text, WhileLoaderWaits whileWaiting = WhileLoaderWaits::ShowNothing);
}