#pragma once

namespace Core {

	// Asked every frame, so it does not read through GLFW, which copies the clipboard and logs
	// GLFW_FORMAT_UNAVAILABLE as an error whenever it holds something else, such as an image.
	// Without a cheap platform query it answers true and leaves the decision to the read.
	bool ClipboardHasText();
}
