#include "Theme/ToastPalette.h"

namespace Graphite {

	Core::ToastPalette ToastPaletteFor(const ResolvedScheme& scheme)
	{
		Core::ToastPalette palette;

		palette.Surface = ColourFromScheme(scheme, ChromeColor::Surface);
		palette.Text = ColourFromScheme(scheme, ChromeColor::Text);
		palette.TextDim = ColourFromScheme(scheme, ChromeColor::TextDisabled);
		palette.Success = ColourFromScheme(scheme, GitColor::GutterAdded);
		palette.Warning = ColourFromScheme(scheme, EditorColor::DiagnosticWarning);
		palette.Error = ColourFromScheme(scheme, EditorColor::DiagnosticError);

		return palette;
	}
}
