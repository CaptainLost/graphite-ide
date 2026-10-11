#pragma once

namespace Core {

	// Says a view is still working out what to show: `text` in the middle of whatever is left
	// of the current window, with a band in the text colour sweeping across it and the letters
	// under the band drawn in the window's background colour. A text wider than the window is
	// drawn smaller to fit. Asks the frame loop for the next frame, so the band keeps moving for
	// as long as a view keeps drawing it.
	void LoadingState(const char* text);
}
