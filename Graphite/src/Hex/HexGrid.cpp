#include "Hex/HexGrid.h"

#include "Hex/HexFile.h"
#include "Hex/HexText.h"
#include "Hex/HexView.h"

#include "Core/ImGui/TextMetrics.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {
		// ADR 0621: lines spaced as the editor spaces them.
		constexpr float kLineHeightFactor = 1.12f;
		constexpr float kHalf = 0.5f;
		constexpr int kColumns = static_cast<int>(kBytesPerRow);
		constexpr int kHalfRow = kColumns / 2;

		// In cells of the code face.
		constexpr float kOffsetGap = 2.0f;
		constexpr float kDigitCells = 2.0f;
		constexpr float kByteCells = 3.0f;
		constexpr float kHalfRowGap = 1.0f;
		constexpr float kAsciiGap = 2.0f;

		constexpr std::string_view kOffsetHeading = "Offset";
		constexpr std::string_view kAsciiHeading = "ASCII";
		constexpr std::string_view kBytePlaceholder = "··";
		constexpr std::string_view kCharacterPlaceholder = "·";

		struct ColumnSpan
		{
			int First = 0;
			int Last = 0;
		};

		void AddText(ImDrawList& drawList, ImVec2 at, ImU32 colour, std::string_view text)
		{
			drawList.AddText(at, colour, text.data(), text.data() + text.size());
		}

		// A printable byte is drawn as itself in the text colour, a full stop among them; only the stand-ins are dim.
		bool IsStandIn(std::uint8_t b)
		{
			return static_cast<std::uint8_t>(AsciiOf(b)) != b;
		}

		// The columns of the row starting at `start`, `count` bytes long, that fall within [first, last].
		std::optional<ColumnSpan> ColumnsWithin(std::uint64_t start, int count, std::uint64_t first, std::uint64_t last)
		{
			const std::uint64_t end = start + static_cast<std::uint64_t>(count);

			if (last < start || first >= end)
			{
				return std::nullopt;
			}

			return ColumnSpan{static_cast<int>(std::max(first, start) - start), static_cast<int>(std::min(last, end - 1) - start)};
		}

		// The hex column's mark breaks at the gap after the eighth byte, as its digits do.
		void Mark(ImDrawList& drawList, const HexGrid& grid, float top, ColumnSpan span, ImU32 colour)
		{
			const float bottom = top + grid.LineHeight;

			for (const ColumnSpan half : {ColumnSpan{0, kHalfRow - 1}, ColumnSpan{kHalfRow, kColumns - 1}})
			{
				const int from = std::max(span.First, half.First);
				const int to = std::min(span.Last, half.Last);

				if (from <= to)
				{
					drawList.AddRectFilled(ImVec2(grid.SlotLeft(from), top), ImVec2(grid.SlotRight(to), bottom), colour);
				}
			}

			drawList.AddRectFilled(ImVec2(grid.CharacterLeft(span.First), top),
								   ImVec2(grid.CharacterLeft(span.Last) + grid.Cell, bottom), colour);
		}

		void MarkRow(ImDrawList& drawList, const HexViewState& state, const EditorPalette& palette, const HexGrid& grid,
					 std::uint64_t start, int count, float top)
		{
			const std::uint64_t first = state.Selection.First();
			const std::uint64_t last = first + state.Selection.Count() - 1;

			if (const std::optional<ColumnSpan> selected = ColumnsWithin(start, count, first, last))
			{
				Mark(drawList, grid, top, *selected, palette.Selection);
			}

			const bool hoveredSelected = state.Hovered && *state.Hovered >= first && *state.Hovered <= last;

			if (state.Hovered && !hoveredSelected)
			{
				if (const std::optional<ColumnSpan> hovered = ColumnsWithin(start, count, *state.Hovered, *state.Hovered))
				{
					Mark(drawList, grid, top, *hovered, ImGui::GetColorU32(ImGuiCol_ButtonHovered));
				}
			}
		}

		// Each column is drawn whole before the next, so a row's digits and its characters each read as one run.
		void DrawRow(ImDrawList& drawList, const HexFile& file, const HexViewState& state, const EditorPalette& palette,
					 const HexGrid& grid, std::uint64_t row, float top)
		{
			const std::uint64_t start = row * kBytesPerRow;
			const int count = static_cast<int>(std::min<std::uint64_t>(kBytesPerRow, file.GetSize() - start));
			const std::vector<std::uint8_t> bytes = file.BytesFrom(start, static_cast<std::size_t>(count));
			const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
			const float textTop = top + grid.TextOffset;

			MarkRow(drawList, state, palette, grid, start, count, top);
			AddText(drawList, ImVec2(grid.OffsetLeft, textTop), dim, OffsetText(start, grid.Digits));

			for (int column = 0; column < count; ++column)
			{
				const ImVec2 at(grid.ByteLeft(column), textTop);
				const auto index = static_cast<std::size_t>(column);

				if (index < bytes.size())
				{
					AddText(drawList, at, bytes[index] == 0 ? dim : palette.Text, ByteText(bytes[index]));
				}
				else
				{
					AddText(drawList, at, dim, kBytePlaceholder);
				}
			}

			for (int column = 0; column < count; ++column)
			{
				const ImVec2 at(grid.CharacterLeft(column), textTop);
				const auto index = static_cast<std::size_t>(column);

				if (index < bytes.size())
				{
					const char shown = AsciiOf(bytes[index]);
					AddText(drawList, at, IsStandIn(bytes[index]) ? dim : palette.Text, std::string_view(&shown, 1));
				}
				else
				{
					AddText(drawList, at, dim, kCharacterPlaceholder);
				}
			}
		}

		std::optional<int> ColumnUnder(const HexGrid& grid, float x)
		{
			for (int column = 0; column < kColumns; ++column)
			{
				const bool inDigits = x >= grid.SlotLeft(column) && x < grid.SlotRight(column);
				const bool inCharacter = x >= grid.CharacterLeft(column) && x < grid.CharacterLeft(column) + grid.Cell;

				if (inDigits || inCharacter)
				{
					return column;
				}
			}

			return std::nullopt;
		}

		// In the column the pointer is nearer to, halfway across the gap between the two.
		int NearestColumn(const HexGrid& grid, float x)
		{
			const bool inAscii = x >= (grid.SlotRight(kColumns - 1) + grid.AsciiLeft) * kHalf;
			int nearest = 0;
			float nearestDistance = FLT_MAX;

			for (int column = 0; column < kColumns; ++column)
			{
				const float middle = inAscii ? grid.CharacterLeft(column) + grid.Cell * kHalf
											 : (grid.SlotLeft(column) + grid.SlotRight(column)) * kHalf;
				const float distance = std::abs(x - middle);

				if (distance < nearestDistance)
				{
					nearest = column;
					nearestDistance = distance;
				}
			}

			return nearest;
		}
	}

	float HexGrid::ByteLeft(int column) const
	{
		const float gap = column >= kHalfRow ? kHalfRowGap : 0.0f;
		return HexLeft + Cell * (kByteCells * static_cast<float>(column) + gap);
	}

	float HexGrid::SlotLeft(int column) const
	{
		return ByteLeft(column) - Cell * (kByteCells - kDigitCells) * kHalf;
	}

	float HexGrid::SlotRight(int column) const
	{
		return SlotLeft(column) + Cell * kByteCells;
	}

	float HexGrid::CharacterLeft(int column) const
	{
		return AsciiLeft + Cell * static_cast<float>(column);
	}

	float HexGrid::HeaderRight() const
	{
		return AsciiLeft + Cell * static_cast<float>(kAsciiHeading.size());
	}

	HexGrid MeasureGrid(float left, std::uint64_t fileSize)
	{
		HexGrid grid;
		const float fontSize = ImGui::GetFontSize();

		grid.Cell = Core::MonospaceCellWidth();
		grid.LineHeight = std::round(kLineHeightFactor * fontSize);
		grid.TextOffset = std::floor((grid.LineHeight - fontSize) * kHalf);
		grid.Digits = OffsetDigits(fileSize);
		grid.OffsetLeft = left;
		grid.HexLeft = left + grid.Cell * (static_cast<float>(grid.Digits) + kOffsetGap);

		// The hex column ends with the last byte's digits, not the space after them.
		const float hexCells = kByteCells * static_cast<float>(kColumns - 1) + kDigitCells + kHalfRowGap;
		grid.AsciiLeft = grid.HexLeft + grid.Cell * (hexCells + kAsciiGap);

		return grid;
	}

	std::uint64_t HexRows::EndRow(const HexScroll& scroll) const
	{
		return std::min(Count, scroll.TopRow + Touching);
	}

	HexRows LayOutRows(const ImRect& region, float barWidth, const HexGrid& grid, std::uint64_t fileSize)
	{
		HexRows rows;
		rows.Area = ImRect(region.Min, ImVec2(region.Max.x - barWidth, region.Max.y));
		rows.Track = ImRect(ImVec2(region.Max.x - barWidth, region.Min.y), region.Max);
		rows.Count = RowCount(fileSize);

		const float lines = region.GetHeight() / grid.LineHeight;
		rows.Visible = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::floor(lines)));
		rows.Touching = static_cast<std::uint64_t>(std::ceil(lines));

		return rows;
	}

	std::optional<std::uint64_t> ByteUnder(const HexGrid& grid, const HexRows& rows, const HexScroll& scroll,
										   std::uint64_t fileSize, ImVec2 point)
	{
		if (!rows.Area.Contains(point))
		{
			return std::nullopt;
		}

		const std::optional<int> column = ColumnUnder(grid, point.x);

		if (!column)
		{
			return std::nullopt;
		}

		const auto line = static_cast<std::uint64_t>((point.y - rows.Area.Min.y) / grid.LineHeight);
		const std::uint64_t offset = (scroll.TopRow + line) * kBytesPerRow + static_cast<std::uint64_t>(*column);

		return offset < fileSize ? std::optional<std::uint64_t>(offset) : std::nullopt;
	}

	std::uint64_t NearestByte(const HexGrid& grid, const HexRows& rows, const HexScroll& scroll, std::uint64_t fileSize,
							  ImVec2 point)
	{
		const float lastLine = static_cast<float>(std::max<std::uint64_t>(rows.Touching, 1) - 1);
		const float line = std::clamp(std::floor((point.y - rows.Area.Min.y) / grid.LineHeight), 0.0f, lastLine);
		const std::uint64_t offset = (scroll.TopRow + static_cast<std::uint64_t>(line)) * kBytesPerRow +
									 static_cast<std::uint64_t>(NearestColumn(grid, point.x));

		return std::min(offset, fileSize - 1);
	}

	void DrawHeader(ImDrawList& drawList, const HexGrid& grid, const ImRect& header)
	{
		const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		const float textTop = header.Min.y + grid.TextOffset;

		drawList.PushClipRect(header.Min, header.Max, true);
		AddText(drawList, ImVec2(grid.OffsetLeft, textTop), dim, kOffsetHeading);

		for (int column = 0; column < kColumns; ++column)
		{
			AddText(drawList, ImVec2(grid.ByteLeft(column), textTop), dim, ByteText(static_cast<std::uint8_t>(column)));
		}

		AddText(drawList, ImVec2(grid.AsciiLeft, textTop), dim, kAsciiHeading);
		drawList.PopClipRect();
	}

	void DrawRows(ImDrawList& drawList, const HexFile& file, const HexViewState& state, const EditorPalette& palette,
				  const HexGrid& grid, const HexRows& rows)
	{
		if (file.GetSize() == 0)
		{
			return;
		}

		drawList.PushClipRect(rows.Area.Min, rows.Area.Max, true);

		for (std::uint64_t row = state.Scroll.TopRow; row < rows.EndRow(state.Scroll); ++row)
		{
			const float top = rows.Area.Min.y + static_cast<float>(row - state.Scroll.TopRow) * grid.LineHeight;
			DrawRow(drawList, file, state, palette, grid, row, top);
		}

		drawList.PopClipRect();
	}
}
