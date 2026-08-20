#include "Editor/Palettes.h"

#include <algorithm>
#include <cmath>

namespace Graphite {

	namespace {

		// The one conversion from Core::Color to ImU32 in this sub-project — everything else
		// reaches ImGui only through the palettes this builds.
		ImU32 ToImU32(const Core::Color& color)
		{
			const auto byte = [](float channel) {
				return static_cast<int>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
			};

			return IM_COL32(byte(color.R), byte(color.G), byte(color.B), byte(color.A));
		}
	}

	SyntaxPalette ReadSyntaxPalette(const ResolvedScheme& scheme)
	{
		SyntaxPalette palette;

		for (std::size_t kind = 0; kind < scheme.Syntax.size(); ++kind)
		{
			palette.Colours[kind] = ToImU32(scheme.Syntax[kind]);
		}

		return palette;
	}

	EditorPalette ReadEditorPalette(const ResolvedScheme& scheme)
	{
		EditorPalette palette;

		palette.Background = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::Background)]);
		palette.Text = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::Text)]);
		palette.Number = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::LineNumber)]);
		palette.Selection = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::Selection)]);
		palette.CurrentLine = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::CurrentLine)]);
		palette.Guide = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::Guide)]);
		palette.GutterFill = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::GutterFill)]);
		palette.Caret = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::Caret)]);
		palette.FindMatch = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::FindMatch)]);
		palette.FindCurrentMatch =
			ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::FindCurrentMatch)]);
		palette.Lens = ToImU32(scheme.Editor[static_cast<std::size_t>(EditorColor::Lens)]);

		return palette;
	}
}
