#pragma once

#include "Core/ImGui/Toasts.h"
#include "Theme/ColorScheme.h"

namespace Graphite {

	// The scheme's colours in the shape Core::Toasts takes. Plain Core::Color like
	// ChromePaletteFor, so it is testable without a window. The card stays on the plain surface
	// colour; the kind's colour goes to its border, softened, and to the circle of its icon.
	// Error and Warning are the editor's diagnostic colours and Success the gutter's added
	// colour, so a toast reads in the same colour the same news has elsewhere in the window.
	Core::ToastPalette ToastPaletteFor(const ResolvedScheme& scheme);
}
