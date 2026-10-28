#pragma once

#include <string>
#include <vector>

namespace Graphite {

	// One text line and a little padding: the height of every chip.
	float ChipHeight();

	// Rounded chips of muted text, left to right, each starting a new line where it would pass the right edge.
	// A row with no chips still ends on an item, one chip high.
	void DrawChipRow(const std::vector<std::string>& chips);
}
