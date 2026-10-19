#pragma once

#include <imgui.h>

namespace Graphite {

	// One segment of a segmented choice: a text button, clear at rest like every other, the chosen one filled with the
	// accent and its word in white. Every segmented choice draws its segments through these two.
	bool DrawSegment(const char* label, bool selected, const ImVec2& size = ImVec2(0.0f, 0.0f));

	// A segment with no frame padding above and below its word, for a choice inside a line of text.
	bool DrawSmallSegment(const char* label, bool selected);
}
