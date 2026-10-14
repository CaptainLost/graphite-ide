#pragma once

#include "Git/GitDecorations.h"

#include <imgui.h>

#include <optional>

namespace Graphite {

	std::optional<ImU32> TabTextColour(GitDecoration decoration, bool preview, ImU32 statusColour,
									   ImU32 disabledColour);
}
