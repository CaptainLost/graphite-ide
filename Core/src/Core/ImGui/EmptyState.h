#pragma once

#include <imgui.h>

#include <string_view>
#include <vector>

namespace Core {

	// One line of a wrapped message, and where it starts relative to the top-left corner of
	// the region it is centred in.
	struct CentredLine
	{
		std::string_view Text;
		ImVec2 Offset;
	};

	// Wraps `text` to `region`'s width in the font currently pushed, centres every line on
	// its own, and centres the lines as a block - with `below` more pixels of whatever the
	// caller draws under them - inside `region`. A block taller or wider than the region
	// starts at its edge rather than above or left of it, so its first line stays in view.
	//
	// Needs a current ImGui context with its fonts built; it measures, it does not draw.
	std::vector<CentredLine> CentreLines(std::string_view text, ImVec2 region, float below = 0.0f);

	// Says why a view has nothing to show, in the middle of whatever is left of the current
	// window, in the disabled text colour. With `action`, a button under the message that
	// returns true on the frame it is clicked.
	//
	// Claims all of that space as one item - more, when the message and its button do not
	// fit, so the window can scroll to them - and anything drawn after it lands below the
	// window's visible area rather than on top of the message. `size` narrows the region
	// for a view that has something under it: a zero component means all that is left.
	bool EmptyState(std::string_view message, const char* action = nullptr, ImVec2 size = ImVec2(0.0f, 0.0f));
}
