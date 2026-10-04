#pragma once

#include "Core/ImGui/Toasts.h"
#include "Theme/ColorScheme.h"

namespace Graphite {

	// The scheme's colours in the shape Core::Toasts takes. Plain Core::Color like
	// ChromePaletteFor, so it is testable without a window. Core tints the surface towards the
	// kind's colour, which sets the card apart from the panels it floats over. Error and Warning are the
	// editor's diagnostic colours and Success the gutter's added colour, so a toast reads in
	// the same colour the same news has elsewhere in the window.
	Core::ToastPalette ToastPaletteFor(const ResolvedScheme& scheme);
}
