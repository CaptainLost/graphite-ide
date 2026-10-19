#pragma once

#include "Git/GitDecorations.h"

#include <imgui.h>

#include <optional>

namespace Graphite {

	// The colour of the dot a tab shows after its name for its file's git status; none for a file without one. The
	// name itself keeps the tab's own colour, preview or not.
	std::optional<ImU32> TabDotColour(GitDecoration decoration, ImU32 statusColour);
}
