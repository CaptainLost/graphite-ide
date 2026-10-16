#pragma once

#include "Theme/ColorScheme.h"
#include "Theme/GitColor.h"

#include <imgui.h>

#include <array>
#include <cstddef>

namespace Graphite {

	struct GitPalette
	{
		ImU32 DiffAddedLine = 0;
		ImU32 DiffRemovedLine = 0;
		ImU32 DiffAddedText = 0;
		ImU32 DiffRemovedText = 0;

		ImU32 GutterAdded = 0;
		ImU32 GutterModified = 0;
		ImU32 GutterRemoved = 0;

		ImU32 LineNumber = 0;

		std::array<ImU32, 6> Lanes{};

		ImU32 LaneOf(std::size_t lane) const { return Lanes[lane % Lanes.size()]; }
	};

	GitPalette ReadGitPalette(const ResolvedScheme& scheme);
}
