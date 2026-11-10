#include "Sessions/Overview/DashboardColours.h"

#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/Theme.h"

#include <imgui_internal.h>

namespace Graphite {

	namespace {
		constexpr float kEmptyHeatLift = 0.06f;
		constexpr std::array<float, kHeatLevels - 1> kHeatShares = {0.3f, 0.5f, 0.75f, 1.0f};
		constexpr float kBarShare = 0.18f;
		constexpr float kHatchAlpha = 0.35f;

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
		ImU32 EmptyHeat(ImU32 surface)
		{
			const ImVec4 channels = ImGui::ColorConvertU32ToFloat4(surface);
			const bool light = Core::IsLightBackground(Core::Color{channels.x, channels.y, channels.z, channels.w});
			return Mix(surface, light ? IM_COL32(0, 0, 0, 255) : IM_COL32(255, 255, 255, 255), kEmptyHeatLift);
		}
	}

	DashboardColours DashboardColoursFrom(const SessionPalette& palette, ImU32 surface)
	{
		DashboardColours colours{};
		colours.Families[static_cast<std::size_t>(ModelFamily::Opus)] = palette.Accent;
		colours.Families[static_cast<std::size_t>(ModelFamily::Sonnet)] = palette.Read;
		colours.Families[static_cast<std::size_t>(ModelFamily::Haiku)] = palette.Edit;
		colours.Families[static_cast<std::size_t>(ModelFamily::Fable)] = palette.Agent;
		colours.Families[static_cast<std::size_t>(ModelFamily::Other)] = palette.Other;

		colours.Heat[0] = EmptyHeat(surface);
		for (std::size_t level = 1; level < kHeatLevels; ++level)
		{
			colours.Heat[level] = Mix(surface, palette.Edit, kHeatShares[level - 1]);
		}

		colours.Hatch = Core::WithAlphaScaled(palette.TextDisabled, kHatchAlpha);
		colours.Bar = Mix(surface, palette.Edit, kBarShare);
		colours.Ink = ImGui::GetColorU32(ImGuiCol_Text);
		colours.Dim = palette.TextDisabled;
		colours.Tile = surface;
		colours.Frame = ImGui::GetColorU32(ImGuiCol_Border);
		colours.Accent = palette.Accent;
		colours.Bad = palette.Failed;
		colours.Good = palette.Edit;
		return colours;
	}
}
