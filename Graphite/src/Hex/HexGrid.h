#pragma once

#include "Hex/HexNavigation.h"
#include "Theme/EditorPalette.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdint>
#include <optional>

namespace Graphite {

	class HexFile;
	struct HexViewState;

	// Where a hex view's columns stand, measured in the code face pushed at the view's size: the offset, the bytes with
	// one space between them and one more after the eighth, then the characters.
	struct HexGrid
	{
		float Cell = 0.0f;
		float LineHeight = 0.0f;
		float TextOffset = 0.0f;
		int Digits = 0;
		float OffsetLeft = 0.0f;
		float HexLeft = 0.0f;
		float AsciiLeft = 0.0f;

		float ByteLeft(int column) const;

		// A byte's two digits with half a cell on either side, so the marks of neighbouring bytes meet.
		float SlotLeft(int column) const;
		float SlotRight(int column) const;

		float CharacterLeft(int column) const;

		// Where the header's last heading ends.
		float HeaderRight() const;
	};

	HexGrid MeasureGrid(float left, std::uint64_t fileSize);

	// The rows' part of a hex view: the area they are drawn in, the scrollbar's strip at its right, and how many rows
	// the file has, fit whole in the area and reach into it.
	struct HexRows
	{
		ImRect Area;
		ImRect Track;
		std::uint64_t Count = 0;

		// Never less than one, so a page always moves.
		std::uint64_t Visible = 1;
		std::uint64_t Touching = 0;

		// One past the last row drawn.
		std::uint64_t EndRow(const HexScroll& scroll) const;
	};

	HexRows LayOutRows(const ImRect& region, float barWidth, const HexGrid& grid, std::uint64_t fileSize);

	// The byte whose digits or character are under `point`, if any.
	std::optional<std::uint64_t> ByteUnder(const HexGrid& grid, const HexRows& rows, const HexScroll& scroll,
										   std::uint64_t fileSize, ImVec2 point);

	// The byte nearest `point`, held to the rows on screen and to the file, which must not be empty.
	std::uint64_t NearestByte(const HexGrid& grid, const HexRows& rows, const HexScroll& scroll, std::uint64_t fileSize,
							  ImVec2 point);

	void DrawHeader(ImDrawList& drawList, const HexGrid& grid, const ImRect& header);

	// The rows on screen with the selection and the hovered byte marked in both columns, and a placeholder for each
	// byte not read yet.
	void DrawRows(ImDrawList& drawList, const HexFile& file, const HexViewState& state, const EditorPalette& palette,
				  const HexGrid& grid, const HexRows& rows);
}
