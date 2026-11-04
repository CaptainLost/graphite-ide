#pragma once

#include "Theme/ColorScheme.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Graphite {

	// What a chip says by its colour: green for a state that needs nothing done (a branch in sync), amber for one that
	// needs a look, blue for information, plain for the rest.
	enum class ChipTone
	{
		Plain,
		Good,
		Warning,
		Info,

		Count
	};

	struct Chip
	{
		std::string Text;
		ChipTone Tone = ChipTone::Plain;

		friend bool operator==(const Chip&, const Chip&) = default;
	};

	struct ChipInk
	{
		ImU32 Fill = 0;
		ImU32 Text = 0;
	};

	struct ChipColours
	{
		std::array<ChipInk, static_cast<std::size_t>(ChipTone::Count)> Tones{};

		const ChipInk& Of(ChipTone tone) const { return Tones[static_cast<std::size_t>(tone)]; }
	};

	// The tones mixed from the scheme's own colours over the current style's, so they follow a light scheme as well.
	ChipColours ReadChipColours(const ResolvedScheme& scheme);

	// One line of the chips' smaller text and a little padding: the height of every chip.
	float ChipHeight();

	// The width of a chip of `text`, in the chips' smaller text.
	float ChipWidthOf(const std::string& text);

	// How many chips fit in `maxLines` lines of `room`, laid left to right and wrapped where one would pass the edge,
	// with room left on the last line for a chip of `plusWidth(hidden)` that counts the ones cut. All of them when
	// they fit, or when `maxLines` is 0.
	std::size_t ChipsShown(const std::vector<float>& widths, float room, float spacing, int maxLines,
						   const std::function<float(std::size_t hidden)>& plusWidth);

	// Pill-shaped chips, left to right, each starting a new line where it would pass the right edge. Past `maxLines`
	// lines the rest are counted in a last "+N" chip whose tooltip names them; 0 draws every line. A row with no chips
	// still ends on an item, one chip high.
	void DrawChipRow(const std::vector<Chip>& chips, const ChipColours& colours, int maxLines = 0);
}
