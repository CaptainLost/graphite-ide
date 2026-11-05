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

		// The pills of a commit's refs. The checked-out branch's fill is the scheme's accent; the rest are fixed colours.
		ImU32 RefHeadFill = IM_COL32(0x42, 0x85, 0xf4, 255);
		ImU32 RefHeadText = IM_COL32(255, 255, 255, 255);
		ImU32 RefLocalFill = IM_COL32(0x2e, 0x59, 0x8d, 255);
		ImU32 RefLocalText = IM_COL32(0xdc, 0xe8, 0xff, 255);
		ImU32 RefRemoteFill = IM_COL32(0x26, 0x30, 0x3e, 255);
		ImU32 RefRemoteText = IM_COL32(0xa9, 0xc6, 0xf5, 255);
		ImU32 RefTagFill = IM_COL32(0x3a, 0x30, 0x20, 255);
		ImU32 RefTagText = IM_COL32(0xf0, 0xcf, 0x8f, 255);

		std::array<ImU32, 6> Lanes{};

		ImU32 LaneOf(std::size_t lane) const { return Lanes[lane % Lanes.size()]; }
	};

	GitPalette ReadGitPalette(const ResolvedScheme& scheme);
}
