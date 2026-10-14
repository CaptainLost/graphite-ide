#include "Theme/ChromePalette.h"

namespace Graphite {

	Core::ChromePalette ChromePaletteFor(const ResolvedScheme& scheme)
	{
		Core::ChromePalette palette;

		palette.Background = ColourFromScheme(scheme, ChromeColor::Background);
		palette.Surface = ColourFromScheme(scheme, ChromeColor::Surface);
		palette.SurfaceHovered = ColourFromScheme(scheme, ChromeColor::SurfaceHovered);
		palette.SurfaceActive = ColourFromScheme(scheme, ChromeColor::SurfaceActive);
		palette.Accent = ColourFromScheme(scheme, ChromeColor::Accent);
		palette.Text = ColourFromScheme(scheme, ChromeColor::Text);
		palette.TextDisabled = ColourFromScheme(scheme, ChromeColor::TextDisabled);
		palette.Border = ColourFromScheme(scheme, ChromeColor::Border);

		return palette;
	}
}
