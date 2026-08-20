#include "Theme/ChromePalette.h"

namespace Graphite {

	namespace {

		const Core::Color& ColorIn(const ResolvedScheme& scheme, ChromeColor color)
		{
			return scheme.Chrome[static_cast<std::size_t>(color)];
		}
	}

	Core::ChromePalette ChromePaletteFor(const ResolvedScheme& scheme)
	{
		Core::ChromePalette palette;

		palette.Background = ColorIn(scheme, ChromeColor::Background);
		palette.Surface = ColorIn(scheme, ChromeColor::Surface);
		palette.SurfaceHovered = ColorIn(scheme, ChromeColor::SurfaceHovered);
		palette.SurfaceActive = ColorIn(scheme, ChromeColor::SurfaceActive);
		palette.Accent = ColorIn(scheme, ChromeColor::Accent);
		palette.Text = ColorIn(scheme, ChromeColor::Text);
		palette.TextDisabled = ColorIn(scheme, ChromeColor::TextDisabled);
		palette.Border = ColorIn(scheme, ChromeColor::Border);

		return palette;
	}
}
