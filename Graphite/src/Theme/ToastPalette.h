#pragma once

#include "Core/ImGui/Toasts.h"
#include "Theme/ColorScheme.h"

namespace Graphite {

	Core::ToastPalette ToastPaletteFor(const ResolvedScheme& scheme);
}
