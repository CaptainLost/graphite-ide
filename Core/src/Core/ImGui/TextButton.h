#pragma once

#include <imgui.h>

namespace Core {

	// The corner radius of a button that shows a word, a pixel less than a field's.
	float TextButtonRounding();

	// A button that shows a word: no fill at rest, the hover surface under the pointer, no border.
	bool TextButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f));

	// A text button with no frame padding above and below its word, for use inside a line of text.
	bool SmallTextButton(const char* label);
}
