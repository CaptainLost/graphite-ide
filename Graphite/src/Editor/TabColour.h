#pragma once

#include "Git/GitDecorations.h"

#include <imgui.h>

#include <optional>

namespace Graphite {

	// The text colour an editor tab is drawn in, or nothing when the tab keeps the theme's own.
	// A decorated file takes its status colour, and a preview tab dims it (alpha scaled) rather than
	// swapping it for the disabled grey, so the status still reads; an undecorated preview is the
	// disabled colour alone, and an undecorated permanent tab is left alone.
	std::optional<ImU32> TabTextColour(GitDecoration decoration, bool preview, ImU32 statusColour,
									   ImU32 disabledColour);
}
