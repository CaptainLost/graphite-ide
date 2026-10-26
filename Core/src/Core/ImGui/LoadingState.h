#pragma once

#include <imgui.h>

#include <cstdint>

namespace Core {

	inline constexpr double kSecondsBeforeLoaderShows = 0.2;

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

	// WARNING: One row high, taken while the loader waits too unless asked to take no room. Answers whether the loader
	// shows yet, so a caller can keep what it drew before for the first fifth of a second.
	bool LoadingLine(const char* text, WhileLoaderWaits whileWaiting = WhileLoaderWaits::ShowNothing);

	float LoadingLineWidth(const char* text);

	// WARNING: The line loader's spinner alone, drawn with no item and no wait of its own, so it can stand in a row's icon
	// slot without moving the row. The caller decides when the wait has gone on long enough to show it.
	void DrawLoadingSpinner(ImVec2 centre);
}