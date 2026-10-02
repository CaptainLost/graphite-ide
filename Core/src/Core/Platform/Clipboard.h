#pragma once

namespace Core {

	// Whether the system clipboard holds text, asked without reading it. Reading through GLFW
	// is the wrong tool for a question asked every frame: it opens the clipboard and copies its
	// contents, and when the clipboard holds something else — an image — it raises
	// GLFW_FORMAT_UNAVAILABLE, which the error callback logs as an error each time.
	//
	// Where the platform has no cheap query this answers true, and the read that follows
	// decides.
	bool ClipboardHasText();
}
