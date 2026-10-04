#include "Theme/ToastPalette.h"

namespace Graphite {

	Core::ToastPalette ToastPaletteFor(const ResolvedScheme& scheme)
	{
		Core::ToastPalette palette;

		palette.Surface = scheme.Chrome[static_cast<std::size_t>(ChromeColor::SurfaceHovered)];
		palette.Border = scheme.Chrome[static_cast<std::size_t>(ChromeColor::Border)];
		palette.Text = scheme.Chrome[static_cast<std::size_t>(ChromeColor::Text)];
		palette.TextDim = scheme.Chrome[static_cast<std::size_t>(ChromeColor::TextDisabled)];
		palette.Success = scheme.Git[static_cast<std::size_t>(GitColor::GutterAdded)];
		palette.Warning = scheme.Editor[static_cast<std::size_t>(EditorColor::DiagnosticWarning)];
		palette.Error = scheme.Editor[static_cast<std::size_t>(EditorColor::DiagnosticError)];

		return palette;
	}
}
