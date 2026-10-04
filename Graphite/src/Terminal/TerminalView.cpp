#include "Terminal/TerminalView.h"

#include "Core/Base/Narrow.h"

#include "Editor/StatusBar/ClipboardCopy.h"
#include "Terminal/TerminalRules.h"
#include "Terminal/TerminalSelection.h"

#include "Core/Command/Chord.h"
#include "Core/ImGui/ImGuiKeys.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/Terminal/KeyEncoding.h"
#include "Core/Text/Utf8.h"

// For SetKeyOwner: taking the mouse wheel away from the window's own scrolling has no
// public entry point, and the same header is already used elsewhere in this project.
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

		// One wheel notch moves this many lines, matching what a scrollbar drag of the same
		// gesture would do elsewhere in the application.
		constexpr int kWheelLines = 3;

		ImU32 ChannelOf(ImU32 colour, int shift)
		{
			return (colour >> shift) & 0xFFu;
		}

		ImU32 WithAlpha(ImU32 colour, ImU32 alpha)
		{
			return (colour & ~(0xFFu << IM_COL32_A_SHIFT)) | ((alpha & 0xFFu) << IM_COL32_A_SHIFT);
		}

		// Several of the editor's own entries are translucent bands meant to be laid over text —
		// the selection, the find match. Only the default foreground and background reach the
		// terminal now, and neither is one of those, but forcing this keeps a scheme that made
		// its text colour slightly transparent from turning every glyph here into a ghost.
		ImU32 Opaque(ImU32 colour)
		{
			return WithAlpha(colour, 255u);
		}

		// xterm's own sixteen, and deliberately not the editor's scheme.
		//
		// The scheme names its colours by meaning rather than by hue, and most of the entries
		// that might have stood in here are translucent bands meant to be painted *behind* text —
		// the selection, the find match, the bracket highlights. Forcing those opaque to serve as
		// foregrounds invents values the theme never offered, and a program asking for red wants
		// the red it was authored against rather than this IDE's error red, which shares the word
		// and not the intent.
		//
		// The cost is real and is a deliberate deferral, not an oversight: the terminal does not
		// visually belong to the active theme the way the rest of the application does. Buying
		// that back wants sixteen named terminal colours a scheme can set — an additive change
		// that belongs with the settings work, not a mapping guessed at from fields chosen for
		// something else.
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

		ImU32 PaletteColour(std::uint8_t index)
		{
			if (index < 16)
			{
				return AnsiColour(index);
			}

			if (index < 232)
			{
				// The 6x6x6 cube, as three base-6 digits. The ramp is xterm's — 0, 95, 135, 175,
				// 215, 255 — and not the linear scaling across 0-255 that it resembles and that
				// looks tidier written down. It is uneven on purpose, and every 256-colour prompt,
				// ls colouring and diff tool picks its indices against these exact values, so
				// "simplifying" this to digit * 255 / 5 silently renders somebody's deliberate
				// choice as a different colour.
				const int offset = static_cast<int>(index) - 16;
				const auto level = [](int digit) { return digit == 0 ? 0 : 55 + digit * 40; };

				return IM_COL32(level(offset / 36), level((offset / 6) % 6), level(offset % 6), 255);
			}

			// xterm's greyscale ramp, from the same source as the cube above.
			const int grey = 8 + (static_cast<int>(index) - 232) * 10;

			return IM_COL32(grey, grey, grey, 255);
		}

		Core::KeyModifier HeldModifiers(const ImGuiIO& io)
		{
			Core::KeyModifier held = Core::KeyModifier::None;

			if (io.KeyShift)
			{
				held = held | Core::KeyModifier::Shift;
			}

			if (io.KeyCtrl)
			{
				held = held | Core::KeyModifier::Control;
			}

			if (io.KeyAlt)
			{
				held = held | Core::KeyModifier::Alt;
			}

			if (io.KeySuper)
			{
				held = held | Core::KeyModifier::Super;
			}

			return held;
		}

		// Inverted from Core::ToImGuiKey rather than written out a second time: a second table
		// would be one more thing to keep in step with the first, and the two disagreeing would
		// show up as a key that fires an IDE command and reaches the shell as well.
		std::optional<Core::KeyCode> ToKeyCode(ImGuiKey key)
		{
			static const std::unordered_map<int, Core::KeyCode> table = [] {
				std::unordered_map<int, Core::KeyCode> built;

				// The enumeration's own range: GLFW's values, of which the last is Menu.
				for (std::uint16_t raw = 0;
					 raw <= static_cast<std::uint16_t>(Core::KeyCode::Menu); ++raw)
				{
					const auto candidate = static_cast<Core::KeyCode>(raw);

					if (const ImGuiKey mapped = Core::ToImGuiKey(candidate); mapped != ImGuiKey_None)
					{
						built.emplace(static_cast<int>(mapped), candidate);
					}
				}

				return built;
			}();

			const auto found = table.find(static_cast<int>(key));

			if (found == table.end())
			{
				return std::nullopt;
			}

			return found->second;
		}
	}

	Core::CursorPosition TerminalView::Draw(const EditorPalette& palette)
	{
		ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont());

		// The cell comes from the baked font, never from CalcTextSize: that rounds an advance up
		// to a whole pixel, and a grid laid out on the rounded value drifts a character away from
		// its own glyphs by the fortieth column.
		const ImVec2 cellSize(Core::MonospaceCellWidth(), ImGui::GetTextLineHeight());
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImVec2 origin = ImGui::GetCursorScreenPos();

		// The scrollbar's strip is taken whether or not there is history to scroll: the grid
		// narrowing by a column the moment the first line scrolls off would resize the session
		// under the reader, and a width that depends on the content is how the fitted size
		// ends up oscillating.
		const float scrollbarWidth = ImGui::GetStyle().ScrollbarSize;

		const int columns =
			std::max(1, static_cast<int>((available.x - scrollbarWidth) / cellSize.x));
		const int rows = std::max(1, static_cast<int>(available.y / cellSize.y));

		const Core::TerminalScreen& screen = m_Session.GetScreen();
		const std::size_t scrollback = screen.GetScrollbackCount();
		const std::uint64_t scrolledOff = screen.GetRowsScrolledOff();

		m_Columns = static_cast<std::size_t>(columns);

		// A view scrolled back stays on the text the reader is looking at. The offset is measured
		// from the tail, so every row the shell pushes into the scrollback would otherwise slide
		// that text up by one; growing the offset by as many rows as went in leaves it put. The
		// growth is capped at the history there is, which the clamp below would do anyway, so a
		// burst of millions of rows cannot overflow the int on its way there.
		if (!m_FollowTail && scrolledOff > m_LastRowsScrolledOff)
		{
			m_ScrollOffset += static_cast<int>(
				std::min<std::uint64_t>(scrolledOff - m_LastRowsScrolledOff, scrollback));
		}

		m_LastRowsScrolledOff = scrolledOff;

		// Its text has all scrolled out of the history, so its line numbers now name nothing. Or the
		// screen switched to or from the alternate buffer: the screen rows it names now hold a
		// different screen's text. A start alone that has gone is moved to the oldest line held.
		const bool alternate = screen.IsAlternateBuffer();

		if (SelectionLeftHistory(m_Selection, scrolledOff, scrollback) || alternate != m_WasAlternate)
		{
			ClearSelection();
		}
		else
		{
			m_Selection = ClampSelectionToHistory(m_Selection, scrolledOff, scrollback);
		}

		m_WasAlternate = alternate;

		if (ImGui::IsWindowHovered())
		{
			// Claimed for as long as the pointer is over the grid, not only on the frames a
			// notch arrives: ImGui applies the wheel to the window's own scroll at the start of
			// the next frame and tests the ownership set during this one. Without this a single
			// notch moves the scrollback and the window together. The owner is an id of this
			// view's own, which is what makes the test in UpdateMouseWheel fail for the window.
			ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetID("##terminalWheel"));

			// A notch at a time rather than truncated to whole lines: a trackpad reports
			// fractions, and truncating those would make it scroll nothing at all.
			if (const float wheel = ImGui::GetIO().MouseWheel; wheel != 0.0f)
			{
				const auto notches =
					static_cast<int>(wheel > 0.0f ? std::ceil(wheel) : std::floor(wheel));

				// A program that asked for the mouse scrolls itself, and is told about each notch
				// at the cell under the pointer. Moving the view instead would do nothing for it:
				// such a program is usually on the alternate buffer, which has no scrollback.
				if (screen.IsMouseReporting())
				{
					const ImVec2 mouse = ImGui::GetMousePos();
					const auto column = static_cast<std::uint16_t>(std::clamp(
						static_cast<int>(std::floor((mouse.x - origin.x) / cellSize.x)), 0, columns - 1));
					const auto row = static_cast<std::uint16_t>(std::clamp(
						static_cast<int>(std::floor((mouse.y - origin.y) / cellSize.y)), 0, rows - 1));
					const Core::KeyModifier held = HeldModifiers(ImGui::GetIO());
					std::string reports;

					for (int notch = 0; notch < std::abs(notches); ++notch)
					{
						reports += Core::EncodeMouseWheel(notches > 0, column, row, held,
														  screen.IsSgrMouse());
					}

					m_Session.Send(reports);
				}
				else
				{
					m_ScrollOffset += notches * kWheelLines;
				}
			}
		}

		m_ScrollOffset = std::clamp(m_ScrollOffset, 0, static_cast<int>(scrollback));
		m_FollowTail = m_ScrollOffset == 0;

		// The strip on the right of the grid, as tall as the rows are. Before the mouse is read
		// for the grid: a press on the track is the scrollbar's and must not also start a
		// selection under it, which the gesture's Claims sees by the grab this sets.
		DrawScrollbar(ImVec2(origin.x + available.x - scrollbarWidth, origin.y),
					  ImVec2(origin.x + available.x, origin.y + static_cast<float>(rows) * cellSize.y),
					  rows, static_cast<int>(scrollback));

		// A sweep dragged above or below the grid scrolls the history a row a frame after the
		// pointer, so a selection can reach further than the grid is tall. Before the gesture
		// reads the pointer, so it lands on the row this scroll brought into view.
		if (m_Gesture.IsSweeping() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			const float mouseY = ImGui::GetMousePos().y;

			if (mouseY < origin.y)
			{
				++m_ScrollOffset;
			}
			else if (mouseY > origin.y + static_cast<float>(rows) * cellSize.y)
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

		// Before the rows are drawn, so the highlight goes under the text of the frame the mouse
		// moved in rather than a frame behind the pointer. The gesture asks to come after every
		// item in the window so an item can take a press first; this window has none that can -
		// the grid and the scrollbar are drawn straight onto the draw list, the Dummy below only
		// reserves space, and the scrollbar keeps its press through Claims - so nothing later in
		// the frame can claim a press the gesture already took.
		UpdateSelection(grid);

		ImDrawList& drawList = *ImGui::GetWindowDrawList();
		std::vector<Core::Cell> scratch;

		for (int view = 0; view < rows; ++view)
		{
			const int row = view - m_ScrollOffset;
			const std::int64_t line = LineOfScreenRow(scrolledOff, row);

			if (const std::vector<Core::Cell>* cells =
					TerminalLineCells(screen, line, static_cast<std::size_t>(columns), scratch);
				cells != nullptr)
			{
				DrawRow(drawList, *cells, row, origin, cellSize, columns, palette);
			}
		}

		// Not while scrolled back: the cursor belongs to the screen's bottom, and drawing it over
		// history would put it on a line the shell is not editing.
		if (screen.IsCursorVisible() && m_FollowTail)
		{
			const Core::CursorPosition cursor = screen.GetCursor();

			if (static_cast<int>(cursor.Column) < columns && static_cast<int>(cursor.Row) < rows && cursor.Column < screen.GetColumns() && cursor.Row < screen.GetRows())
			{
				const ImVec2 topLeft(origin.x + static_cast<float>(cursor.Column) * cellSize.x,
									 origin.y + static_cast<float>(cursor.Row) * cellSize.y);

				// A double-width character occupies two cells and its glyph is drawn across
				// both, so a cursor one cell wide would invert half of the very character it
				// exists to keep readable.
				const bool wide =
					static_cast<int>(cursor.Column) + 1 < columns && cursor.Column + 1 < screen.GetColumns() && screen.At(cursor.Row, static_cast<std::uint16_t>(cursor.Column + 1)).WideTrailer;

				const ImVec2 bottomRight(topLeft.x + (wide ? 2.0f : 1.0f) * cellSize.x,
										 topLeft.y + cellSize.y);

				if (ImGui::IsWindowFocused())
				{
					// Inverted rather than filled over, which is what a real console does and the
					// parity this feature is held to. A solid block hides the character underneath
					// it, and it hides it exactly where it matters least affordably: on the one
					// character you are about to overtype.
					const Core::Cell& cell = screen.At(cursor.Row, cursor.Column);

					ImU32 foreground = 0;
					ImU32 background = 0;

					ResolveCell(cell, palette, foreground, background, true);

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
					// Hollow while the keyboard is elsewhere: it still says where typing would
					// land, without claiming that it would.
					drawList.AddRect(topLeft, bottomRight, palette.Caret);
				}
			}
		}

		HandleKeyboard();

		ImGui::PopFont();

		// The grid is drawn straight onto the draw list, which ImGui knows nothing about. Without
		// an item covering it the cursor never advances, the scroll extent collapses to nothing
		// and the item protocol trips its own assert on the last row of the frame. The spacing
		// comes off because ItemSize adds it back: rows is the region's height divided down to
		// whole cells, so the remainder is less than one cell and usually less than the default
		// spacing — reserving the full height would overflow the region, raise a scrollbar,
		// narrow the next frame's width by one column, and set the fitted size oscillating.
		// Clamped at zero for the pathological case of a mono font shorter than the spacing.
		const float height = std::max(
			0.0f, static_cast<float>(rows) * cellSize.y - ImGui::GetStyle().ItemSpacing.y);

		ImGui::Dummy(ImVec2(static_cast<float>(columns) * cellSize.x, height));

		Core::CursorPosition fitted;
		fitted.Row = static_cast<std::uint16_t>(rows);
		fitted.Column = static_cast<std::uint16_t>(columns);

		return fitted;
	}

	void TerminalView::DrawScrollbar(const ImVec2& trackMin, const ImVec2& trackMax, int rows,
									 int scrollback)
	{
		// Nothing to scroll through: no bar, and a drag that was under way has nothing left to
		// hold on to.
		if (scrollback <= 0)
		{
			m_ScrollbarGrab = -1.0f;
			return;
		}

		const float trackHeight = trackMax.y - trackMin.y;
		const float minThumb = ImGui::GetStyle().GrabMinSize;
		const ImVec2 mouse = ImGui::GetMousePos();

		TerminalScrollbarThumb thumb =
			ComputeTerminalScrollbarThumb(m_ScrollOffset, rows, scrollback, trackHeight, minThumb);

		const bool overTrack =
			ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(trackMin, trackMax);

		if (overTrack && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			ImGui::SetWindowFocus();

			const float thumbTop = trackMin.y + thumb.Top;

			// A press on the thumb keeps it under the pointer where it was grabbed; a press on
			// the track elsewhere brings the thumb to the pointer, centred, and carries on as a
			// drag - which is what a click and a click-and-drag both want.
			m_ScrollbarGrab = mouse.y >= thumbTop && mouse.y < thumbTop + thumb.Height
								  ? mouse.y - thumbTop
								  : thumb.Height * 0.5f;
		}

		if (m_ScrollbarGrab >= 0.0f)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				m_ScrollbarGrab = -1.0f;
			}
			else
			{
				m_ScrollOffset = TerminalScrollOffsetForThumbTop(
					mouse.y - trackMin.y - m_ScrollbarGrab, scrollback, trackHeight, thumb.Height);

				thumb = ComputeTerminalScrollbarThumb(m_ScrollOffset, rows, scrollback,
													  trackHeight, minThumb);
			}
		}

		const bool dragging = m_ScrollbarGrab >= 0.0f;
		const bool overThumb = overTrack && mouse.y >= trackMin.y + thumb.Top &&
							   mouse.y < trackMin.y + thumb.Top + thumb.Height;

		ImDrawList& drawList = *ImGui::GetWindowDrawList();

		drawList.AddRectFilled(trackMin, trackMax, ImGui::GetColorU32(ImGuiCol_ScrollbarBg));
		drawList.AddRectFilled(
			ImVec2(trackMin.x, trackMin.y + thumb.Top),
			ImVec2(trackMax.x, trackMin.y + thumb.Top + thumb.Height),
			ImGui::GetColorU32(dragging	  ? ImGuiCol_ScrollbarGrabActive
							   : overThumb ? ImGuiCol_ScrollbarGrabHovered
										   : ImGuiCol_ScrollbarGrab),
			ImGui::GetStyle().ScrollbarRounding);
	}

	bool TerminalView::DrawContextMenu()
	{
		bool close = false;

		// Over the grid rather than over the tab strip: it is the text a reader has just
		// selected that they want to do something with, and the gesture lands where that text
		// is. The window is the whole of this view, so the right button anywhere in it opens it.
		if (!ImGui::BeginPopupContextWindow("##terminalContext",
											ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverExistingPopup))
		{
			return false;
		}

		// The chords that only ever do what the item says: plain Ctrl+C is an interrupt when
		// nothing is selected, so it is not the one to advertise. Ctrl+V pastes too.
		if (ImGui::MenuItem("Copy", "Ctrl+Shift+C", false, HasSelection()))
		{
			Copy();
		}

		// No chord beside it: Ctrl+A is the shell's, which moves to the start of the line.
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

		// Reported rather than performed: the session lives in the panel's tab vector, and
		// removing it from inside the draw the tab item is part of would pull the ground out
		// from under ImGui mid-frame.
		if (ImGui::MenuItem("Close"))
		{
			close = true;
		}

		ImGui::EndPopup();

		return close;
	}

	void TerminalView::Copy()
	{
		if (!HasSelection())
		{
			return;
		}

		if (const std::string selected = CopySelection(); !selected.empty())
		{
			CopyToClipboard(m_Events, "selection", selected);
		}

		// Cleared once copied, so the next Ctrl+C is the interrupt again (ADR 0546).
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
		// The scrollback goes with the screen, which is the choice `clear` itself makes on a
		// modern system — it emits ESC[H ESC[2J ESC[3J, and the third of those is exactly "drop
		// the history". Erasing only what is visible would leave everything the reader asked to
		// be rid of one wheel notch above the top of an apparently empty screen, which reads as
		// the command having half worked. What is lost is recoverable by rerunning a command;
		// what is gained is that the gesture means what it says.
		Core::TerminalScreen& screen = m_Session.GetScreen();

		screen.EraseInDisplay(Core::EraseExtent::All);
		screen.SetCursor(0, 0);
		screen.ClearScrollback();

		m_ScrollOffset = 0;
		m_FollowTail = true;
		ClearSelection();
	}

	void TerminalView::DrawRow(ImDrawList& drawList, const std::vector<Core::Cell>& cells, int row,
							   const ImVec2& origin, const ImVec2& cellSize, int columns,
							   const EditorPalette& palette)
	{
		const int count = std::min(columns, Core::Narrow<int>(cells.size()));

		if (count <= 0)
		{
			return;
		}

		const float top = origin.y + static_cast<float>(row + m_ScrollOffset) * cellSize.y;
		const float bottom = top + cellSize.y;

		const auto xOf = [&origin, &cellSize](int column) {
			return origin.x + static_cast<float>(column) * cellSize.x;
		};

		// Members rather than locals: a pair of vectors allocated per row per frame is thousands
		// of allocations a second for buffers whose size barely changes.
		std::vector<ImU32>& foregrounds = m_RowForegrounds;
		std::vector<ImU32>& backgrounds = m_RowBackgrounds;

		foregrounds.resize(static_cast<std::size_t>(count));
		backgrounds.resize(static_cast<std::size_t>(count));

		for (int column = 0; column < count; ++column)
		{
			const Core::Cell& cell = cells[static_cast<std::size_t>(column)];

			ImU32 foreground = 0;
			ImU32 background = 0;

			ResolveCell(cell, palette, foreground, background);

			foregrounds[static_cast<std::size_t>(column)] = foreground;
			backgrounds[static_cast<std::size_t>(column)] = background;
		}

		// Backgrounds, then selection, then glyphs — three passes over the row rather than one
		// pass per cell. A cell at a time would be four and a half thousand draw calls a frame on
		// a 120x40 grid; coalescing runs that share a colour makes it a handful.
		for (int column = 0; column < count;)
		{
			const ImU32 colour = backgrounds[static_cast<std::size_t>(column)];
			int end = column + 1;

			while (end < count && backgrounds[static_cast<std::size_t>(end)] == colour)
			{
				++end;
			}

			// The window has already painted its own background, so a cell that never asked for
			// one costs nothing.
			if (colour != palette.Background)
			{
				drawList.AddRectFilled(ImVec2(xOf(column), top), ImVec2(xOf(end), bottom), colour);
			}

			column = end;
		}

		const std::int64_t textLine = LineOfScreenRow(m_Session.GetScreen().GetRowsScrolledOff(), row);

		if (const std::optional<Core::ColumnSpan> span =
				m_Selection.SpanOn(textLine, static_cast<std::size_t>(count)))
		{
			// The scheme's own selection colour, as the editor draws its selection.
			Core::DrawSelectionSpan(drawList, xOf(static_cast<int>(span->From)),
									xOf(static_cast<int>(span->To)), top, cellSize.y,
									palette.Selection);
		}

		std::string run;

		for (int column = 0; column < count;)
		{
			const Core::Cell& first = cells[static_cast<std::size_t>(column)];

			// The right half of a double-width character: its glyph was drawn by the cell to its
			// left, and drawing anything here would put a second copy over that half.
			if (first.WideTrailer)
			{
				++column;
				continue;
			}

			if (Core::HasFlag(first.Flags, Core::CellFlags::Hidden))
			{
				++column;
				continue;
			}

			const ImU32 colour = foregrounds[static_cast<std::size_t>(column)];
			const Core::CellFlags flags = first.Flags;

			run.clear();
			int end = column;

			while (end < count)
			{
				const Core::Cell& cell = cells[static_cast<std::size_t>(end)];

				if (cell.WideTrailer || cell.Flags != flags ||
					foregrounds[static_cast<std::size_t>(end)] != colour)
				{
					break;
				}

				Core::AppendUtf8(run, cell.Code);
				++end;

				// A double-width character ends the run. One AddText call advances by the face's
				// own per-glyph widths, and this face has no glyph twice as wide as a cell — it
				// substitutes something one cell wide — so everything after it in the same call
				// would sit a column left of the cells it belongs to.
				if (end < count && cells[static_cast<std::size_t>(end)].WideTrailer)
				{
					break;
				}
			}

			if (!run.empty())
			{
				drawList.AddText(ImVec2(xOf(column), top), colour, run.c_str(),
								 run.c_str() + run.size());

				if (Core::HasFlag(flags, Core::CellFlags::Underline))
				{
					const float line = bottom - 1.0f;

					drawList.AddLine(ImVec2(xOf(column), line), ImVec2(xOf(end), line), colour);
				}
			}

			column = end > column ? end : column + 1;
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
			std::vector<Core::Cell> scratch;
			const std::vector<Core::Cell>* cells = TerminalLineCells(screen, at.Line, grid.Columns, scratch);

			return cells == nullptr ? Core::ColumnSpan{} : TerminalWordAt(*cells, grid.Columns, at.Column);
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
		// The scrollbar took the press this frame, or is being dragged: it is the scrollbar's.
		source.Claims = [this](ImVec2) {
			return m_ScrollbarGrab >= 0.0f;
		};

		Core::SelectionGestureOptions options;
		options.ScrollsVertically = false;
		options.ScrollsHorizontally = false;
		options.CopyKey = false;
		options.SelectAllKey = false;
		options.MenuId = nullptr;

		// With its keys and menu off the gesture never copies, so there is nothing to report.
		static_cast<void>(m_Gesture.Update(m_Selection, source, options));
	}

	void TerminalView::HandleKeyboard()
	{
		if (!ImGui::IsWindowFocused())
		{
			return;
		}

		ImGuiIO& io = ImGui::GetIO();

		// Taken as characters rather than encoded from the key, because the host has already
		// applied the keyboard layout to them — which is also why EncodeKey deliberately returns
		// nothing for a printable key, so that nothing is sent twice. TerminalTakesTypedText says
		// when they count as typing; it is what lets AltGr through, which the host reports as
		// Alt alone.
		const Core::KeyModifier held = HeldModifiers(io);
		std::string typed;

		if (TerminalTakesTypedText(held))
		{
			for (int index = 0; index < io.InputQueueCharacters.Size; ++index)
			{
				Core::AppendUtf8(typed, static_cast<char32_t>(io.InputQueueCharacters[index]));
			}

			if (!typed.empty())
			{
				m_Session.Send(typed);
			}
		}

		// Whether anything was typed is the only evidence that an Alt press was AltGr rather
		// than a chord of its own, so the drain has to happen before the modifiers are decided.
		const Core::KeyModifier modifiers = TerminalKeyModifiers(held, !typed.empty());

		for (ImGuiKey key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END;
			 key = static_cast<ImGuiKey>(static_cast<int>(key) + 1))
		{
			if (!ImGui::IsKeyPressed(key, true))
			{
				continue;
			}

			const std::optional<Core::KeyCode> code = ToKeyCode(key);

			if (!code.has_value())
			{
				continue;
			}

			// A modifier on its own sends nothing; it is the key it is held with that does.
			if (Core::ModifierBitOf(*code) != 0)
			{
				continue;
			}

			// The view's own gestures, judged before the binding lookup below rather than after
			// it. Every outcome of Ctrl+C resolves here, not just the copy: the interrupt is the one
			// that must not depend on what the IDE has bound, and letting the empty selection fall
			// through to the lookup would put it back at the mercy of it. A paste likewise has to
			// arrive as text on the wire rather than as an editor action, which is why Ctrl+V is
			// settled here and not left to whatever an editor binds to it.
			switch (ClassifyTerminalShortcut(*code, held, HasSelection()))
			{
			case TerminalShortcut::Copy:
				Copy();
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

			// Anything the IDE would take stays the IDE's: a chord bound to a command that fires
			// while a terminal holds the keyboard. This one rule is what keeps Ctrl+P opening the
			// file finder while Ctrl+A, Ctrl+E, Ctrl+W and Ctrl+R reach the shell's line editing:
			// nothing else distinguishes them, and a terminal that took every key would make the
			// rest of the application unreachable from inside it. Asked as Global because that is
			// the scope the shortcut router fires from here; a chord only an editor can use is
			// bound to nothing as far as this view is concerned. The chord is built the way
			// KeyBindingsSection builds one, so the two agree about what a binding is.
			const Core::Chord chord = Core::Chord::Key(io.KeyMods | key);

			if (!m_Keys.BoundTo(chord, Core::Chord::Scope::Global, m_Commands).empty())
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
								bool foreground) const
	{
		switch (color.Kind)
		{
		// Only Default reaches the panel's own colours. Everything the program named
		// explicitly is given as named — see AnsiColour for why the theme is not consulted.
		case Core::TerminalColorKind::Palette:
			return PaletteColour(color.Index);
		case Core::TerminalColorKind::Rgb:
			return IM_COL32(color.R, color.G, color.B, 255);
		case Core::TerminalColorKind::Default:
			break;
		}

		return foreground ? Opaque(palette.Text) : palette.Background;
	}

	void TerminalView::ResolveCell(const Core::Cell& cell, const EditorPalette& palette,
								   ImU32& foreground, ImU32& background, bool opaqueBase) const
	{
		// Bold on one of the low eight palette entries is the bright entry eight above it. That
		// is what every terminal since the hardware ones has done with it, and without it
		// "\e[1;31m" and "\e[31m" are the same red — which is every bold prompt and every
		// heading git status prints. A bold cell that named a 256-colour index or an RGB colour
		// is left alone: the program picked an exact colour and there is no brighter one to
		// promote it to.
		//
		// Italic is parsed and carried on the cell but is not drawn: this application bakes one
		// monospaced face and has no italic one to switch to, and slanting the upright face by
		// hand would smear a grid whose whole premise is that every cell is the same width.
		Core::TerminalColor chosen = cell.Foreground;

		if (Core::HasFlag(cell.Flags, Core::CellFlags::Bold) && chosen.Kind == Core::TerminalColorKind::Palette && chosen.Index < 8)
		{
			chosen.Index = static_cast<std::uint8_t>(chosen.Index + 8);
		}

		foreground = Resolve(chosen, palette, true);
		background = Resolve(cell.Background, palette, false);

		// Before the flags below rather than after them, which is what keeps a faint character
		// faint under the cursor: the cursor needs both colours opaque, because it paints the
		// background as a glyph over the foreground as a block and a scheme carrying alpha
		// would let the block show through, but forcing that once the halving had been applied
		// would undo it and render the one faint character the cursor sits on at full strength.
		if (opaqueBase)
		{
			foreground = Opaque(foreground);
			background = Opaque(background);
		}

		// Inverse is the program's, not the theme's: swapping the two resolved colours is what
		// makes a shell's highlighted completion or a status line read as inverted whatever
		// scheme it is being drawn in.
		if (Core::HasFlag(cell.Flags, Core::CellFlags::Inverse))
		{
			std::swap(foreground, background);
		}

		if (Core::HasFlag(cell.Flags, Core::CellFlags::Faint))
		{
			foreground = WithAlpha(foreground, ChannelOf(foreground, IM_COL32_A_SHIFT) / 2u);
		}
	}

	std::string TerminalView::CopySelection() const
	{
		return SelectedTerminalText(m_Selection, m_Session.GetScreen(), SelectionColumns());
	}
}
