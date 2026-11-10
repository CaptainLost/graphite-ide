#pragma once

#include "Sessions/Overview/DashboardModel.h"
#include "Theme/SessionPalette.h"

#include <imgui.h>

#include <array>
#include <cstddef>

namespace Graphite {

	inline constexpr std::size_t kHeatLevels = 5;

	struct DashboardColours
	{
		std::array<ImU32, static_cast<std::size_t>(ModelFamily::Count)> Families;
		std::array<ImU32, kHeatLevels> Heat;
		ImU32 Hatch;
		ImU32 Bar;
		ImU32 Ink;
		ImU32 Dim;
		ImU32 Tile;
		ImU32 Frame;
		ImU32 Accent;
		ImU32 Bad;
		ImU32 Good;
		ImU32 TipSurface; // sunk below the tiles, away from the frame
		ImU32 Shadow;	  // under a tip card, its alpha the whole shadow's
		ImU32 Pill;		  // the accent laid thinly on the surface
	};

	bool IsLightSurface(ImU32 surface);

	// WARNING: Reads the style's text colour, so it runs inside a frame.
	DashboardColours DashboardColoursFrom(const SessionPalette& palette, ImU32 surface);
}
