#include "Terminal/TerminalView.h"

#include "Core/Base/Narrow.h"

#include "UI/ClipboardCopy.h"
#include "Terminal/TerminalRules.h"
#include "Terminal/TerminalSelection.h"

#include "Core/Command/Chord.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/ImGuiKeys.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/Terminal/KeyEncoding.h"
#include "Core/Text/Utf8.h"

// WARNING: SetKeyOwner has no public entry point, and the same header is already used elsewhere in this project.
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		constexpr int kWheelLines = 3;
		constexpr float kHalf = 0.5f;
		constexpr float kUnderlineInset = 1.0f;
		constexpr float kSingleCellWidth = 1.0f;
		constexpr float kDoubleCellWidth = 2.0f;

		constexpr int kOpaqueAlpha = 255;
		constexpr int kAnsiColourCount = 16;
		constexpr int kBaseColourCount = 8;
		constexpr int kBrightColourOffset = 8;
		constexpr int kFirstCubeIndex = 16;
		constexpr int kFirstGreyIndex = 232;
		constexpr int kCubeSide = 6;
		constexpr int kCubeSideSquared = 36;
		constexpr int kCubeLevelBase = 55;
		constexpr int kCubeLevelStep = 40;
		constexpr int kGreyBase = 8;
		constexpr int kGreyStep = 10;
		constexpr ImU32 kFaintDivisor = 2u;

		// WARNING: These are xterm's own sixteen colours, deliberately not the editor's scheme: most
		// of its entries are translucent bands meant to sit behind text, and a program asking for
		// red wants xterm's red rather than the IDE's error red.
		ImU32 AnsiColour(std::uint8_t index)
		{
			switch (index)
			{
			case 0:
				return IM_COL32(0x00, 0x00, 0x00, 0xFF);
			case 1:
				return IM_COL32(0xCD, 0x00, 0x00, 0xFF);
			case 2:
				return IM_COL32(0x00, 0xCD, 0x00, 0xFF);
			case 3:
				return IM_COL32(0xCD, 0xCD, 0x00, 0xFF);
			case 4:
				return IM_COL32(0x00, 0x00, 0xEE, 0xFF);
			case 5:
				return IM_COL32(0xCD, 0x00, 0xCD, 0xFF);
			case 6:
				return IM_COL32(0x00, 0xCD, 0xCD, 0xFF);
			case 7:
				return IM_COL32(0xE5, 0xE5, 0xE5, 0xFF);
			case 8:
				return IM_COL32(0x7F, 0x7F, 0x7F, 0xFF);
			case 9:
				return IM_COL32(0xFF, 0x00, 0x00, 0xFF);
			case 10:
				return IM_COL32(0x00, 0xFF, 0x00, 0xFF);
			case 11:
				return IM_COL32(0xFF, 0xFF, 0x00, 0xFF);
			case 12:
				return IM_COL32(0x5C, 0x5C, 0xFF, 0xFF);
			case 13:
				return IM_COL32(0xFF, 0x00, 0xFF, 0xFF);
			case 14:
				return IM_COL32(0x00, 0xFF, 0xFF, 0xFF);
			default:
				break;
			}

			return IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
		}

		// WARNING: xterm's cube ramp is uneven on purpose (0, 95, 135, 175, 215, 255). Simplifying
		// it to digit * 255 / 5 renders every 256-colour program in a different colour.
		int CubeLevel(int digit)
		{
			return digit == 0 ? 0 : kCubeLevelBase + digit * kCubeLevelStep;
		}

		ImU32 PaletteColour(std::uint8_t index)
		{
			if (index < kAnsiColourCount)
			{
				return AnsiColour(index);
			}

			if (index < kFirstGreyIndex)
			{
				const int cubeOffset = static_cast<int>(index) - kFirstCubeIndex;
				const int redDigit = cubeOffset / kCubeSideSquared;
				const int greenDigit = (cubeOffset / kCubeSide) % kCubeSide;
				const int blueDigit = cubeOffset % kCubeSide;

				return IM_COL32(CubeLevel(redDigit), CubeLevel(greenDigit), CubeLevel(blueDigit),
								kOpaqueAlpha);
			}

			const int greyLevel = kGreyBase + (static_cast<int>(index) - kFirstGreyIndex) * kGreyStep;

			return IM_COL32(greyLevel, greyLevel, greyLevel, kOpaqueAlpha);
		}

		Core::KeyModifier HeldModifiers(const ImGuiIO& imguiIo)
		{
			Core::KeyModifier held = Core::KeyModifier::None;

			if (imguiIo.KeyShift)
			{
				held = held | Core::KeyModifier::Shift;
			}

			if (imguiIo.KeyCtrl)
			{
				held = held | Core::KeyModifier::Control;
			}

			if (imguiIo.KeyAlt)
			{
				held = held | Core::KeyModifier::Alt;
			}

			if (imguiIo.KeySuper)
			{
				held = held | Core::KeyModifier::Super;
			}

			return held;
		}

		std::unordered_map<int, Core::KeyCode> BuildKeyCodeTable()
		{
			std::unordered_map<int, Core::KeyCode> table;

			for (std::uint16_t rawCode = 0;
				 rawCode <= static_cast<std::uint16_t>(Core::KeyCode::Menu); ++rawCode)
			{
				const auto candidate = static_cast<Core::KeyCode>(rawCode);

				if (const ImGuiKey mapped = Core::ToImGuiKey(candidate); mapped != ImGuiKey_None)
				{
					table.emplace(static_cast<int>(mapped), candidate);
				}
			}

			return table;
		}

		std::optional<Core::KeyCode> ToKeyCode(ImGuiKey key)
		{
			static const std::unordered_map<int, Core::KeyCode> table = BuildKeyCodeTable();

			const int keyNumber = static_cast<int>(key);

			if (!table.contains(keyNumber))
			{
				return std::nullopt;
			}

			return table.at(keyNumber);
		}

		float ColumnLeft(const ImVec2& origin, const ImVec2& cellSize, int column)
		{
			return origin.x + static_cast<float>(column) * cellSize.x;
		}

		bool CursorIsOnGrid(const Core::TerminalScreen& screen, const Core::CursorPosition& cursor,
							int columns, int rows)
		{
			const bool insideFittedGrid =
				static_cast<int>(cursor.Column) < columns && static_cast<int>(cursor.Row) < rows;
			const bool insideScreen =
				cursor.Column < screen.GetColumns() && cursor.Row < screen.GetRows();

			return insideFittedGrid && insideScreen;
		}

		bool CursorCellIsWide(const Core::TerminalScreen& screen, const Core::CursorPosition& cursor,
							  int columns)
		{
			if (static_cast<int>(cursor.Column) + 1 >= columns)
			{
				return false;
			}

			if (cursor.Column + 1 >= screen.GetColumns())
			{
				return false;
			}

			const std::uint16_t nextColumn = static_cast<std::uint16_t>(cursor.Column + 1);

			return screen.At(cursor.Row, nextColumn).WideTrailer;
		}

		Core::ColumnSpan WordAtPoint(const Core::TerminalScreen& screen, const TerminalGrid& grid,
									 Core::TextPoint point)
		{
			std::vector<Core::TerminalCell> lineScratch;
			const std::vector<Core::TerminalCell>* cells =
				TerminalLineCells(screen, point.Line, grid.Columns, lineScratch);

			if (cells == nullptr)
			{
				return Core::ColumnSpan{};
			}

			return TerminalWordAt(*cells, grid.Columns, point.Column);
		}

		std::string WheelReports(const Core::TerminalScreen& screen, int notches, const ImVec2& origin,
								 const ImVec2& cellSize, int columns, int rows)
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			const int mouseColumn =
				static_cast<int>(std::floor((mouse.x - origin.x) / cellSize.x));
			const int mouseRow = static_cast<int>(std::floor((mouse.y - origin.y) / cellSize.y));
			const auto reportColumn = static_cast<std::uint16_t>(std::clamp(mouseColumn, 0, columns - 1));
			const auto reportRow = static_cast<std::uint16_t>(std::clamp(mouseRow, 0, rows - 1));
			const Core::KeyModifier held = HeldModifiers(ImGui::GetIO());
			const Core::WheelDirection direction =
				notches > 0 ? Core::WheelDirection::Up : Core::WheelDirection::Down;
			const Core::MouseReportForm form =
				screen.IsSgrMouse() ? Core::MouseReportForm::SgrDecimal : Core::MouseReportForm::Legacy;
			std::string reports;

			for (int notch = 0; notch < std::abs(notches); ++notch)
			{
				reports += Core::EncodeMouseWheel(direction, reportColumn, reportRow, held, form);
			}

			return reports;
		}
	}

	Core::CursorPosition TerminalView::Draw(const EditorPalette& palette)
	{
		Core::PushCodeFont();

		// WARNING: The cell width comes from the baked font, never from CalcTextSize. That rounds an
		// advance up to a whole pixel, and a grid laid out on it drifts a character off its glyphs
		// by the fortieth column.
		const ImVec2 cellSize(Core::MonospaceCellWidth(), ImGui::GetTextLineHeight());
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImVec2 origin = ImGui::GetCursorScreenPos();

		const float scrollbarWidth = ImGui::GetStyle().ScrollbarSize;

		const int columns =
			std::max(1, static_cast<int>((available.x - scrollbarWidth) / cellSize.x));
		const int rows = std::max(1, static_cast<int>(available.y / cellSize.y));

		const Core::TerminalScreen& screen = m_Session.GetScreen();
		const std::size_t scrollback = screen.GetScrollbackCount();
		const std::uint64_t scrolledOff = screen.GetRowsScrolledOff();

		m_Columns = static_cast<std::size_t>(columns);

		if (!m_FollowTail && scrolledOff > m_LastRowsScrolledOff)
		{
			m_ScrollOffset += static_cast<int>(
				std::min<std::uint64_t>(scrolledOff - m_LastRowsScrolledOff, scrollback));
		}

		m_LastRowsScrolledOff = scrolledOff;

		const bool isAlternateBuffer = screen.IsAlternateBuffer();

		if (SelectionLeftHistory(m_Selection, scrolledOff, scrollback) ||
			isAlternateBuffer != m_WasAlternate)
		{
			ClearSelection();
		}
		else
		{
			m_Selection = ClampSelectionToHistory(m_Selection, scrolledOff, scrollback);
		}

		m_WasAlternate = isAlternateBuffer;

		if (ImGui::IsWindowHovered())
		{
			// WARNING: The wheel is claimed on every hovered frame, not only when a notch arrives.
			// ImGui applies the wheel to the window's own scroll at the start of the next frame and
			// tests the ownership set during this one.
			ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetID("##terminalWheel"));

			if (const float wheel = ImGui::GetIO().MouseWheel; wheel != 0.0f)
			{
				const int notches =
					static_cast<int>(wheel > 0.0f ? std::ceil(wheel) : std::floor(wheel));

				if (screen.IsMouseReporting())
				{
					m_Session.Send(WheelReports(screen, notches, origin, cellSize, columns, rows));
				}
				else
				{
					m_ScrollOffset += notches * kWheelLines;
				}
			}
		}

		m_ScrollOffset = std::clamp(m_ScrollOffset, 0, static_cast<int>(scrollback));
		m_FollowTail = m_ScrollOffset == 0;

		const float gridBottom = origin.y + static_cast<float>(rows) * cellSize.y;

		DrawScrollbar(ImVec2(origin.x + available.x - scrollbarWidth, origin.y),
					  ImVec2(origin.x + available.x, gridBottom), rows, static_cast<int>(scrollback));

		if (m_Gesture.IsSweeping() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			const float mouseY = ImGui::GetMousePos().y;

			if (mouseY < origin.y)
			{
				++m_ScrollOffset;
			}
			else if (mouseY > gridBottom)
			{
				--m_ScrollOffset;
			}
		}

		m_ScrollOffset = std::clamp(m_ScrollOffset, 0, static_cast<int>(scrollback));
		m_FollowTail = m_ScrollOffset == 0;

		TerminalGrid grid;
		grid.OriginX = origin.x;
		grid.OriginY = origin.y;
		grid.CellWidth = cellSize.x;
		grid.CellHeight = cellSize.y;
		grid.Columns = static_cast<std::size_t>(columns);
		grid.VisibleRows = rows;
		grid.ScrollOffset = m_ScrollOffset;

		UpdateSelection(grid);

		ImDrawList& drawList = *ImGui::GetWindowDrawList();
		std::vector<Core::TerminalCell> lineScratch;

		for (int visibleRow = 0; visibleRow < rows; ++visibleRow)
		{
			const int row = visibleRow - m_ScrollOffset;
			const std::int64_t line = LineOfScreenRow(scrolledOff, row);

			if (const std::vector<Core::TerminalCell>* cells =
					TerminalLineCells(screen, line, static_cast<std::size_t>(columns), lineScratch);
				cells != nullptr)
			{
				DrawRow(drawList, *cells, row, origin, cellSize, columns, palette);
			}
		}

		if (screen.IsCursorVisible() && m_FollowTail)
		{
			const Core::CursorPosition cursor = screen.GetCursor();

			if (CursorIsOnGrid(screen, cursor, columns, rows))
			{
				const ImVec2 topLeft(origin.x + static_cast<float>(cursor.Column) * cellSize.x,
									 origin.y + static_cast<float>(cursor.Row) * cellSize.y);

				const float cursorCells =
					CursorCellIsWide(screen, cursor, columns) ? kDoubleCellWidth : kSingleCellWidth;

				const ImVec2 bottomRight(topLeft.x + cursorCells * cellSize.x, topLeft.y + cellSize.y);

				if (ImGui::IsWindowFocused())
				{
					const Core::TerminalCell& cell = screen.At(cursor.Row, cursor.Column);

					ImU32 foreground = 0;
					ImU32 background = 0;

					ResolveCell(cell, palette, foreground, background, BaseOpacity::ForceOpaque);

					drawList.AddRectFilled(topLeft, bottomRight, foreground);

					if (!Core::HasFlag(cell.Flags, Core::CellFlags::Hidden) && !cell.WideTrailer)
					{
						std::string glyph;
						Core::AppendUtf8(glyph, cell.Code);

						drawList.AddText(topLeft, background, glyph.c_str(),
										 glyph.c_str() + glyph.size());
					}
				}
				else
				{
					drawList.AddRect(topLeft, bottomRight, palette.Caret);
				}
			}
		}

		HandleKeyboard();

		ImGui::PopFont();

		// WARNING: The grid is drawn straight onto the draw list, so an item must cover it or the
		// item protocol asserts. ItemSize adds the spacing back, so it comes off here, or the region
		// overflows, raises a scrollbar, narrows the next frame by a column and the fitted size oscillates.
		const float dummyHeight = std::max(
			0.0f, static_cast<float>(rows) * cellSize.y - ImGui::GetStyle().ItemSpacing.y);

		ImGui::Dummy(ImVec2(static_cast<float>(columns) * cellSize.x, dummyHeight));

		Core::CursorPosition fitted;
		fitted.Row = static_cast<std::uint16_t>(rows);
		fitted.Column = static_cast<std::uint16_t>(columns);

		return fitted;
	}

	void TerminalView::DrawScrollbar(const ImVec2& trackMin, const ImVec2& trackMax, int rows,
									 int scrollback)
	{
		if (scrollback <= 0)
		{
			m_ScrollbarGrab = kNoScrollbarGrab;
			return;
		}

		const float trackHeight = trackMax.y - trackMin.y;
		const float minimumThumbHeight = ImGui::GetStyle().GrabMinSize;
		const ImVec2 mouse = ImGui::GetMousePos();

		TerminalScrollbarThumb thumb = ComputeTerminalScrollbarThumb(
			m_ScrollOffset, rows, scrollback, trackHeight, minimumThumbHeight);

		const bool overTrack =
			ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(trackMin, trackMax);

		if (overTrack && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			ImGui::SetWindowFocus();

			const float thumbTop = trackMin.y + thumb.Top;
			const bool pressedOnThumb = mouse.y >= thumbTop && mouse.y < thumbTop + thumb.Height;

			m_ScrollbarGrab = pressedOnThumb ? mouse.y - thumbTop : thumb.Height * kHalf;
		}

		if (IsScrollbarGrabbed())
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				m_ScrollbarGrab = kNoScrollbarGrab;
			}
			else
			{
				m_ScrollOffset = TerminalScrollOffsetForThumbTop(
					mouse.y - trackMin.y - m_ScrollbarGrab, scrollback, trackHeight, thumb.Height);

				thumb = ComputeTerminalScrollbarThumb(m_ScrollOffset, rows, scrollback, trackHeight,
													  minimumThumbHeight);
			}
		}

		const bool dragging = IsScrollbarGrabbed();
		const bool overThumb = overTrack && mouse.y >= trackMin.y + thumb.Top &&
							   mouse.y < trackMin.y + thumb.Top + thumb.Height;

		ImDrawList& drawList = *ImGui::GetWindowDrawList();

		drawList.AddRectFilled(trackMin, trackMax, ImGui::GetColorU32(ImGuiCol_ScrollbarBg));
		drawList.AddRectFilled(
			ImVec2(trackMin.x, trackMin.y + thumb.Top),
			ImVec2(trackMax.x, trackMin.y + thumb.Top + thumb.Height),
			ImGui::GetColorU32(dragging	   ? ImGuiCol_ScrollbarGrabActive
							   : overThumb ? ImGuiCol_ScrollbarGrabHovered
										   : ImGuiCol_ScrollbarGrab),
			ImGui::GetStyle().ScrollbarRounding);
	}

	bool TerminalView::DrawContextMenu()
	{
		bool closeRequested = false;
		constexpr bool kNotChecked = false;

		const bool clickedHere = ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
								 ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
								 !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);

		if (!Core::BeginRightClickMenu("##terminalContext", clickedHere))
		{
			return false;
		}

		if (ImGui::MenuItem("Copy", "Ctrl+Shift+C", kNotChecked, HasSelection()))
		{
			Copy(CopyConfirm::Toast);
		}

		if (ImGui::MenuItem("Select All"))
		{
			SelectAll();
		}

		if (ImGui::MenuItem("Paste", "Ctrl+Shift+V"))
		{
			PasteFromClipboard();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Clear"))
		{
			Clear();
		}

		if (ImGui::MenuItem("Close"))
		{
			closeRequested = true;
		}

		ImGui::EndPopup();

		return closeRequested;
	}

	void TerminalView::Copy(CopyConfirm confirm)
	{
		if (!HasSelection())
		{
			return;
		}

		if (const std::string selected = CopySelection(); !selected.empty())
		{
			if (confirm == CopyConfirm::Toast)
			{
				CopyToClipboard(m_Events, "selection", selected);
			}
			else
			{
				ImGui::SetClipboardText(selected.c_str());
			}
		}

		ClearSelection();
	}

	void TerminalView::SelectAll()
	{
		const auto [from, to] = TerminalEverything(m_Session.GetScreen(), SelectionColumns());

		m_Gesture.Cancel();
		m_Selection.Select(from, to);
	}

	std::size_t TerminalView::SelectionColumns() const
	{
		return m_Columns != 0 ? m_Columns : static_cast<std::size_t>(m_Session.GetScreen().GetColumns());
	}

	void TerminalView::PasteFromClipboard()
	{
		if (const char* clipboard = ImGui::GetClipboardText(); clipboard != nullptr)
		{
			m_Session.Paste(clipboard);
		}
	}

	void TerminalView::Clear()
	{
		constexpr std::uint16_t kHomeRow = 0;
		constexpr std::uint16_t kHomeColumn = 0;

		Core::TerminalScreen& screen = m_Session.GetScreen();

		screen.EraseInDisplay(Core::EraseExtent::All);
		screen.SetCursor(kHomeRow, kHomeColumn);
		screen.ClearScrollback();

		m_ScrollOffset = 0;
		m_FollowTail = true;
		ClearSelection();
	}

	void TerminalView::DrawRow(ImDrawList& drawList, const std::vector<Core::TerminalCell>& cells, int row,
							   const ImVec2& origin, const ImVec2& cellSize, int columns,
							   const EditorPalette& palette)
	{
		const int cellCount = std::min(columns, Core::Narrow<int>(cells.size()));

		if (cellCount <= 0)
		{
			return;
		}

		const float top = origin.y + static_cast<float>(row + m_ScrollOffset) * cellSize.y;
		const float bottom = top + cellSize.y;

		std::vector<ImU32>& foregrounds = m_RowForegrounds;
		std::vector<ImU32>& backgrounds = m_RowBackgrounds;

		foregrounds.resize(static_cast<std::size_t>(cellCount));
		backgrounds.resize(static_cast<std::size_t>(cellCount));

		for (int column = 0; column < cellCount; ++column)
		{
			const Core::TerminalCell& cell = cells[static_cast<std::size_t>(column)];

			ImU32 foreground = 0;
			ImU32 background = 0;

			ResolveCell(cell, palette, foreground, background);

			foregrounds[static_cast<std::size_t>(column)] = foreground;
			backgrounds[static_cast<std::size_t>(column)] = background;
		}

		for (int column = 0; column < cellCount;)
		{
			const ImU32 colour = backgrounds[static_cast<std::size_t>(column)];
			int runEnd = column + 1;

			while (runEnd < cellCount && backgrounds[static_cast<std::size_t>(runEnd)] == colour)
			{
				++runEnd;
			}

			if (colour != palette.Background)
			{
				drawList.AddRectFilled(ImVec2(ColumnLeft(origin, cellSize, column), top),
									   ImVec2(ColumnLeft(origin, cellSize, runEnd), bottom), colour);
			}

			column = runEnd;
		}

		const std::int64_t textLine = LineOfScreenRow(m_Session.GetScreen().GetRowsScrolledOff(), row);

		if (const std::optional<Core::ColumnSpan> span =
				m_Selection.SpanOn(textLine, static_cast<std::size_t>(cellCount)))
		{
			Core::DrawSelectionSpan(drawList, ColumnLeft(origin, cellSize, static_cast<int>(span->From)),
									ColumnLeft(origin, cellSize, static_cast<int>(span->To)), top,
									cellSize.y, palette.Selection);
		}

		std::string runText;

		for (int column = 0; column < cellCount;)
		{
			const Core::TerminalCell& firstCell = cells[static_cast<std::size_t>(column)];

			if (firstCell.WideTrailer)
			{
				++column;
				continue;
			}

			if (Core::HasFlag(firstCell.Flags, Core::CellFlags::Hidden))
			{
				++column;
				continue;
			}

			const ImU32 colour = foregrounds[static_cast<std::size_t>(column)];
			const Core::CellFlags flags = firstCell.Flags;

			runText.clear();
			int runEnd = column;

			while (runEnd < cellCount)
			{
				const Core::TerminalCell& cell = cells[static_cast<std::size_t>(runEnd)];

				if (cell.WideTrailer || cell.Flags != flags ||
					foregrounds[static_cast<std::size_t>(runEnd)] != colour)
				{
					break;
				}

				Core::AppendUtf8(runText, cell.Code);
				++runEnd;

				// WARNING: A double-width character ends the run. One AddText call advances by the
				// face's per-glyph widths and this face has no glyph twice a cell wide, so anything
				// after it in the same call would sit a column left of its cell.
				if (runEnd < cellCount && cells[static_cast<std::size_t>(runEnd)].WideTrailer)
				{
					break;
				}
			}

			if (!runText.empty())
			{
				drawList.AddText(ImVec2(ColumnLeft(origin, cellSize, column), top), colour,
								 runText.c_str(), runText.c_str() + runText.size());

				if (Core::HasFlag(flags, Core::CellFlags::Underline))
				{
					const float underlineY = bottom - kUnderlineInset;

					drawList.AddLine(ImVec2(ColumnLeft(origin, cellSize, column), underlineY),
									 ImVec2(ColumnLeft(origin, cellSize, runEnd), underlineY), colour);
				}
			}

			column = runEnd > column ? runEnd : column + 1;
		}
	}

	void TerminalView::UpdateSelection(const TerminalGrid& grid)
	{
		const Core::TerminalScreen& screen = m_Session.GetScreen();
		Core::SelectionSource source;

		source.PointAt = [&screen, &grid](ImVec2 at) {
			return TerminalPointAt(screen, grid, at.x, at.y);
		};
		source.CharacterAt = [&screen, &grid](ImVec2 at) {
			return TerminalCharacterAt(screen, grid, at.x, at.y);
		};
		source.WordAt = [&screen, &grid](Core::TextPoint at) {
			return WordAtPoint(screen, grid, at);
		};
		source.LineLength = [&screen, &grid](std::int64_t line) {
			return TerminalLineLength(screen, line, grid.Columns);
		};
		source.Everything = [&screen, &grid]() {
			return TerminalEverything(screen, grid.Columns);
		};
		source.SelectedText = [this]() {
			return CopySelection();
		};
		source.Claims = [this](ImVec2) {
			return IsScrollbarGrabbed();
		};

		Core::SelectionGestureOptions options;
		options.ScrollsVertically = false;
		options.ScrollsHorizontally = false;
		options.CopyKey = false;
		options.SelectAllKey = false;
		options.MenuId = nullptr;

		static_cast<void>(m_Gesture.Update(m_Selection, source, options));
	}

	void TerminalView::HandleKeyboard()
	{
		if (!ImGui::IsWindowFocused())
		{
			return;
		}

		constexpr bool kRepeatWhileHeld = true;

		ImGuiIO& imguiIo = ImGui::GetIO();

		const Core::KeyModifier held = HeldModifiers(imguiIo);
		std::string typed;

		if (TerminalTakesTypedText(held))
		{
			for (int index = 0; index < imguiIo.InputQueueCharacters.Size; ++index)
			{
				Core::AppendUtf8(typed, static_cast<char32_t>(imguiIo.InputQueueCharacters[index]));
			}

			if (!typed.empty())
			{
				m_Session.Send(typed);
			}
		}

		const bool anythingTyped = !typed.empty();
		const Core::KeyModifier modifiers = TerminalKeyModifiers(held, anythingTyped);

		for (ImGuiKey key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END;
			 key = static_cast<ImGuiKey>(static_cast<int>(key) + 1))
		{
			if (!ImGui::IsKeyPressed(key, kRepeatWhileHeld))
			{
				continue;
			}

			const std::optional<Core::KeyCode> code = ToKeyCode(key);

			if (!code.has_value())
			{
				continue;
			}

			const bool isModifierKey = Core::ModifierBitOf(*code) != 0;

			if (isModifierKey)
			{
				continue;
			}

			switch (ClassifyTerminalShortcut(*code, held, HasSelection()))
			{
			case TerminalShortcut::Copy:
				Copy(CopyConfirm::Silent);
				continue;
			case TerminalShortcut::Paste:
				PasteFromClipboard();
				continue;
			case TerminalShortcut::Interrupt:
				if (const std::string bytes = Core::EncodeKey(*code, modifiers); !bytes.empty())
				{
					m_Session.Send(bytes);
				}

				continue;
			case TerminalShortcut::Swallow:
				continue;
			case TerminalShortcut::None:
				break;
			}

			const Core::Chord chord = Core::Chord::Key(imguiIo.KeyMods | key);

			if (!m_Keys.BoundTo(chord, Core::ShortcutScope::Global, m_Commands).empty())
			{
				continue;
			}

			if (const std::string bytes = Core::EncodeKey(*code, modifiers); !bytes.empty())
			{
				m_Session.Send(bytes);
			}
		}
	}

	ImU32 TerminalView::Resolve(const Core::TerminalColor& color, const EditorPalette& palette,
								ColourRole role) const
	{
		switch (color.Kind)
		{
		case Core::TerminalColorKind::Palette:
			return PaletteColour(color.Index);
		case Core::TerminalColorKind::Rgb:
			return IM_COL32(color.R, color.G, color.B, kOpaqueAlpha);
		case Core::TerminalColorKind::Default:
			break;
		}

		return role == ColourRole::Foreground ? Core::WithOpaqueAlpha(palette.Text) : palette.Background;
	}

	void TerminalView::ResolveCell(const Core::TerminalCell& cell, const EditorPalette& palette,
								   ImU32& foreground, ImU32& background, BaseOpacity baseOpacity) const
	{
		Core::TerminalColor chosen = cell.Foreground;

		const bool boldOnBaseColour = Core::HasFlag(cell.Flags, Core::CellFlags::Bold) &&
									  chosen.Kind == Core::TerminalColorKind::Palette &&
									  chosen.Index < kBaseColourCount;

		if (boldOnBaseColour)
		{
			chosen.Index = static_cast<std::uint8_t>(chosen.Index + kBrightColourOffset);
		}

		foreground = Resolve(chosen, palette, ColourRole::Foreground);
		background = Resolve(cell.Background, palette, ColourRole::Background);

		// WARNING: The steps run in this order on purpose: opaque, then inverse, then faint. Forcing
		// opaque after the halving would render a faint character under the cursor at full strength.
		if (baseOpacity == BaseOpacity::ForceOpaque)
		{
			foreground = Core::WithOpaqueAlpha(foreground);
			background = Core::WithOpaqueAlpha(background);
		}

		if (Core::HasFlag(cell.Flags, Core::CellFlags::Inverse))
		{
			std::swap(foreground, background);
		}

		if (Core::HasFlag(cell.Flags, Core::CellFlags::Faint))
		{
			foreground = Core::WithAlphaReplaced(foreground, Core::AlphaByteOf(foreground) / kFaintDivisor);
		}
	}

	std::string TerminalView::CopySelection() const
	{
		return SelectedTerminalText(m_Selection, m_Session.GetScreen(), SelectionColumns());
	}
}
