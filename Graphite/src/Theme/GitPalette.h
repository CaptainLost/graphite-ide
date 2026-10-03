#pragma once

#include "Theme/ColorScheme.h"
#include "Theme/GitColor.h"

#include <imgui.h>

#include <array>
#include <cstddef>

namespace Graphite {

	// The git colours as ImGui wants them, read once a frame.
	struct GitPalette
	{
		ImU32 DiffAddedLine = 0;
		ImU32 DiffRemovedLine = 0;
		ImU32 DiffAddedText = 0;
		ImU32 DiffRemovedText = 0;

		ImU32 GutterAdded = 0;
		ImU32 GutterModified = 0;
		ImU32 GutterRemoved = 0;

		std::array<ImU32, 6> Lanes{};

		// Wraps rather than clamping: a seventh branch shares the first lane's colour,
		// which reads as a coincidence. Clamping would put every lane past the sixth on
		// one colour, which reads as a bug.
		ImU32 LaneOf(std::size_t lane) const { return Lanes[lane % Lanes.size()]; }
	};

	GitPalette ReadGitPalette(const ResolvedScheme& scheme);
}
