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
		palette.Content = ColourFromScheme(scheme, EditorColor::Background);

		palette.Metrics.PanelGap = scheme.Layout[static_cast<std::size_t>(LayoutMetric::PanelGap)];
		palette.Metrics.PanelRounding = scheme.Layout[static_cast<std::size_t>(LayoutMetric::PanelRounding)];
		palette.Metrics.ControlRounding = scheme.Layout[static_cast<std::size_t>(LayoutMetric::ControlRounding)];
		palette.Metrics.BorderWidth = scheme.Layout[static_cast<std::size_t>(LayoutMetric::BorderWidth)];
		palette.Metrics.ScrollbarWidth = scheme.Layout[static_cast<std::size_t>(LayoutMetric::ScrollbarWidth)];

		return palette;
	}
}
