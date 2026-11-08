#pragma once

#include <imgui.h>

namespace Graphite {

	// WCAG's ratio for body text, which terminals that adjust colours for contrast use as well.
	inline constexpr float kReadableContrast = 4.5f;

	// The contrast ratio of two colours, from 1 (the same) to 21 (black on white); alpha is ignored.
	float ContrastRatio(ImU32 first, ImU32 second);

	// The foreground as it is when it reads against the background; otherwise moved toward `towards`, the scheme's own
	// text colour, just far enough that it does. xterm's yellow and white were drawn on a light scheme's near-white
	// background as they are, and could not be read. Keeps the foreground's alpha.
	ImU32 ReadableOn(ImU32 foreground, ImU32 background, ImU32 towards, float minimum = kReadableContrast);
}
