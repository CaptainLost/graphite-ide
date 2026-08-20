#pragma once

#include "Core/ImGui/Theme.h"
#include "Theme/ColorScheme.h"

namespace Graphite {

	// The scheme's chrome colours in the shape Core::Theme::Apply takes. Lives in Theme/ rather
	// than beside the editor palettes because nothing here touches ImGui — Core::ChromePalette
	// is plain Core::Color — so this stays testable without a window.
	Core::ChromePalette ChromePaletteFor(const ResolvedScheme& scheme);
}
