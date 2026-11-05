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

		// The pills of a commit's refs, mixed from the scheme's accent, surface, background, text and warning colours: the
		// checked-out branch on the accent, a local branch on an accent tint, a remote one on a surface, a tag on a
		// warning tint.
		ImU32 RefHeadFill = 0;
		ImU32 RefHeadText = 0;
		ImU32 RefLocalFill = 0;
		ImU32 RefLocalText = 0;
		ImU32 RefRemoteFill = 0;
		ImU32 RefRemoteText = 0;
		ImU32 RefTagFill = 0;
		ImU32 RefTagText = 0;

		std::array<ImU32, 6> Lanes{};

		ImU32 LaneOf(std::size_t lane) const { return Lanes[lane % Lanes.size()]; }
	};

	GitPalette ReadGitPalette(const ResolvedScheme& scheme);
}
