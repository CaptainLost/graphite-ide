#pragma once

#include <imgui.h>

#include <string_view>
#include <vector>

namespace Graphite {

	// One filled face of the mark, in the SVG's own units. A gradient keeps only its first
	// and last stops; a flat fill has both colours equal.
	struct LogoFace
	{
		std::vector<ImVec2> Points;
		ImVec2 From;
		ImVec2 To;
		ImU32 FromColour = 0;
		ImU32 ToColour = 0;
	};

	struct LogoShape
	{
		// In paint order: a later face overlaps an earlier one's edges.
		std::vector<LogoFace> Faces;
		ImVec2 Min;
		ImVec2 Max;
	};

	// Reads the mark out of an SVG: every <path> directly under <svg>, in order. Anything else
	// — the disc behind the mark is a <circle> — is background and left out.
	//
	// A path may use only absolute M, L and Z, one outline each, and be filled with "#rrggbb"
	// or a linear gradient from <defs>. Anything outside that empties the whole shape rather
	// than leave a face out, so a change to the SVG the reader cannot follow shows as a
	// missing mark, not a broken one.
	LogoShape ParseLogoSvg(std::string_view svg);
}
