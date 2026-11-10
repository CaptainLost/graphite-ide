#include "Sessions/Overview/DashboardColours.h"

#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/Theme.h"

#include <imgui_internal.h>

namespace Graphite {

	namespace {
		constexpr float kEmptyHeatLift = 0.06f;
		constexpr float kFrameLift = 0.1f;
		constexpr std::array<float, kHeatLevels - 1> kHeatShares = {0.3f, 0.5f, 0.75f, 1.0f};
		constexpr float kBarShare = 0.18f;
		constexpr float kHatchAlpha = 0.35f;
		constexpr float kTipSink = 0.6f;
		constexpr float kPillShare = 0.15f;
		constexpr int kDarkShadowAlpha = 128;
		constexpr int kLightShadowAlpha = 46;

		ImU32 Mix(ImU32 from, ImU32 to, float share)
		{
			if (share >= 1.0f)
			{
				return to;
			}

			return ImGui::ColorConvertFloat4ToU32(
				ImLerp(ImGui::ColorConvertU32ToFloat4(from), ImGui::ColorConvertU32ToFloat4(to), share));
		}

		// WARNING: "Lightened" on a dark surface; on a light one the same step goes toward black, or an empty day
		// would vanish into the tile.
		ImU32 Lifted(ImU32 surface, float share)
		{
			return Mix(surface, IsLightSurface(surface) ? IM_COL32(0, 0, 0, 255) : IM_COL32(255, 255, 255, 255), share);
		}

		// WARNING: The other way from Lifted: darker on a dark surface, lighter on a light one, so text on it stands out
		// more than on the tiles.
		ImU32 Sunk(ImU32 surface, float share)
		{
			return Mix(surface, IsLightSurface(surface) ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255), share);
		}
	}

	bool IsLightSurface(ImU32 surface)
	{
		const ImVec4 channels = ImGui::ColorConvertU32ToFloat4(surface);
		return Core::IsLightBackground(Core::Color{channels.x, channels.y, channels.z, channels.w});
	}

	DashboardColours DashboardColoursFrom(const SessionPalette& palette, ImU32 surface)
	{
		DashboardColours colours{};
		colours.Families[static_cast<std::size_t>(ModelFamily::Opus)] = palette.Accent;
		colours.Families[static_cast<std::size_t>(ModelFamily::Sonnet)] = palette.Read;
		colours.Families[static_cast<std::size_t>(ModelFamily::Haiku)] = palette.Edit;
		colours.Families[static_cast<std::size_t>(ModelFamily::Fable)] = palette.Agent;
		colours.Families[static_cast<std::size_t>(ModelFamily::Other)] = palette.Other;

		colours.Good = palette.Run;
		colours.Heat[0] = Lifted(surface, kEmptyHeatLift);
		for (std::size_t level = 1; level < kHeatLevels; ++level)
		{
			colours.Heat[level] = Mix(surface, colours.Good, kHeatShares[level - 1]);
		}

		colours.Hatch = Core::WithAlphaScaled(palette.TextDisabled, kHatchAlpha);
		colours.Bar = Mix(surface, colours.Good, kBarShare);
		colours.Ink = ImGui::GetColorU32(ImGuiCol_Text);
		colours.Dim = palette.TextDisabled;
		colours.Tile = surface;
		// WARNING: Not the style's border, which on the default theme is the surface's own colour: tiles, rules and tip
		// cards stand on the surface, where that border would not show.
		colours.Frame = Lifted(surface, kFrameLift);
		colours.Accent = palette.Accent;
		colours.Bad = palette.Failed;
		colours.TipSurface = Sunk(surface, kTipSink);
		colours.Shadow = IM_COL32(0, 0, 0, IsLightSurface(surface) ? kLightShadowAlpha : kDarkShadowAlpha);
		colours.Pill = Mix(surface, palette.Accent, kPillShare);
		return colours;
	}
}
