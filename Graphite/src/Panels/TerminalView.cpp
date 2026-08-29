#include "Panels/TerminalView.h"

#include "Core/Command/Chord.h"
#include "Core/ImGui/ImGuiKeys.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/Term/KeyEncoding.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

		void AppendUtf8(std::string& text, char32_t code)
		{
			const auto value = static_cast<std::uint32_t>(code);

			if (value < 0x80u)
			{
				text.push_back(static_cast<char>(value));
				return;
			}

			if (value < 0x800u)
			{
				text.push_back(static_cast<char>(0xC0u | (value >> 6)));
				text.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
				return;
			}

			if (value < 0x10000u)
			{
				text.push_back(static_cast<char>(0xE0u | (value >> 12)));
				text.push_back(static_cast<char>(0x80u | ((value >> 6) & 0x3Fu)));
				text.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
				return;
			}

			if (value <= 0x10FFFFu)
			{
				text.push_back(static_cast<char>(0xF0u | (value >> 18)));
				text.push_back(static_cast<char>(0x80u | ((value >> 12) & 0x3Fu)));
				text.push_back(static_cast<char>(0x80u | ((value >> 6) & 0x3Fu)));
				text.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
				return;
			}

			// A code point outside Unicode reached the screen, which means the byte stream was
			// malformed. Showing the replacement is better than emitting invalid UTF-8 into a
			// string the clipboard and the draw list both take as valid.
			text.append("\xEF\xBF\xBD");
		}

		ImU32 ChannelOf(ImU32 colour, int shift)
		{
			return (colour >> shift) & 0xFFu;
		}

		ImU32 WithAlpha(ImU32 colour, ImU32 alpha)
		{
			return (colour & ~(0xFFu << IM_COL32_A_SHIFT)) | ((alpha & 0xFFu) << IM_COL32_A_SHIFT);
		}

		// Several of the editor's own entries are translucent bands meant to be laid over text —
		// the selection, the find match — and the ones borrowed below are not, but forcing this
		// keeps a scheme that made its foreground slightly transparent from turning every glyph
		// in the terminal into a ghost.
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
				case 0: return IM_COL32(0x00, 0x00, 0x00, 0xFF);
				case 1: return IM_COL32(0xCD, 0x00, 0x00, 0xFF);
				case 2: return IM_COL32(0x00, 0xCD, 0x00, 0xFF);
				case 3: return IM_COL32(0xCD, 0xCD, 0x00, 0xFF);
				case 4: return IM_COL32(0x00, 0x00, 0xEE, 0xFF);
				case 5: return IM_COL32(0xCD, 0x00, 0xCD, 0xFF);
				case 6: return IM_COL32(0x00, 0xCD, 0xCD, 0xFF);
				case 7: return IM_COL32(0xE5, 0xE5, 0xE5, 0xFF);
				case 8: return IM_COL32(0x7F, 0x7F, 0x7F, 0xFF);
				case 9: return IM_COL32(0xFF, 0x00, 0x00, 0xFF);
				case 10: return IM_COL32(0x00, 0xFF, 0x00, 0xFF);
				case 11: return IM_COL32(0xFF, 0xFF, 0x00, 0xFF);
				case 12: return IM_COL32(0x5C, 0x5C, 0xFF, 0xFF);
				case 13: return IM_COL32(0xFF, 0x00, 0xFF, 0xFF);
				case 14: return IM_COL32(0x00, 0xFF, 0xFF, 0xFF);
				default: break;
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

		Core::KeyModifier ModifiersOf(const ImGuiIO& io)
		{
			Core::KeyModifier modifiers = Core::KeyModifier::None;

			if (io.KeyShift)
			{
				modifiers = modifiers | Core::KeyModifier::Shift;
			}

			if (io.KeyCtrl)
			{
				modifiers = modifiers | Core::KeyModifier::Control;
			}

			if (io.KeyAlt)
			{
				modifiers = modifiers | Core::KeyModifier::Alt;
			}

			if (io.KeySuper)
			{
				modifiers = modifiers | Core::KeyModifier::Super;
			}

			return modifiers;
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

		const int columns = std::max(1, static_cast<int>(available.x / cellSize.x));
		const int rows = std::max(1, static_cast<int>(available.y / cellSize.y));

		const Core::TerminalScreen& screen = m_Session.GetScreen();
		const std::size_t scrollback = screen.GetScrollbackCount();

		// A view scrolled back stays on the text the reader is looking at. The offset is measured
		// from the tail, so every line the shell pushes into the scrollback would otherwise slide
		// that text up by one; growing the offset by as much as the tail moved leaves it put.
		if (!m_FollowTail && scrollback > m_LastScrollbackCount)
		{
			m_ScrollOffset += static_cast<int>(scrollback - m_LastScrollbackCount);
		}

		m_LastScrollbackCount = scrollback;

		if (ImGui::IsWindowHovered())
		{
			// A notch at a time rather than truncated to whole lines: a trackpad reports
			// fractions, and truncating those would make it scroll nothing at all.
			if (const float wheel = ImGui::GetIO().MouseWheel; wheel != 0.0f)
			{
				const auto notches =
					static_cast<int>(wheel > 0.0f ? std::ceil(wheel) : std::floor(wheel));

				m_ScrollOffset += notches * kWheelLines;
			}
		}

		m_ScrollOffset = std::clamp(m_ScrollOffset, 0, static_cast<int>(scrollback));
		m_FollowTail = m_ScrollOffset == 0;

		// Before the rows are drawn, so the highlight goes under the text of the frame the mouse
		// moved in rather than a frame behind the pointer.
		HandleMouse(origin, cellSize, rows, columns);

		ImDrawList& drawList = *ImGui::GetWindowDrawList();
		std::vector<Core::Cell> scratch;

		for (int view = 0; view < rows; ++view)
		{
			const int row = view - m_ScrollOffset;

			if (const std::vector<Core::Cell>* cells = RowCells(row, scratch, columns);
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

			if (static_cast<int>(cursor.Column) < columns && static_cast<int>(cursor.Row) < rows
				&& cursor.Column < screen.GetColumns() && cursor.Row < screen.GetRows())
			{
				const ImVec2 topLeft(origin.x + static_cast<float>(cursor.Column) * cellSize.x,
									 origin.y + static_cast<float>(cursor.Row) * cellSize.y);
				const ImVec2 bottomRight(topLeft.x + cellSize.x, topLeft.y + cellSize.y);

				if (ImGui::IsWindowFocused())
				{
					// Inverted rather than filled over, which is what a real console does and the
					// parity this feature is held to. A solid block hides the character underneath
					// it, and it hides it exactly where it matters least affordably: on the one
					// character you are about to overtype.
					const Core::Cell& cell = screen.At(cursor.Row, cursor.Column);

					ImU32 foreground = 0;
					ImU32 background = 0;

					ResolveCell(cell, palette, foreground, background);

					drawList.AddRectFilled(topLeft, bottomRight, foreground);

					if (!Core::HasFlag(cell.Flags, Core::CellFlags::Hidden) && !cell.WideTrailer)
					{
						std::string glyph;
						AppendUtf8(glyph, cell.Code);

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
		// and the item protocol trips its own assert on the last row of the frame.
		ImGui::Dummy(ImVec2(static_cast<float>(columns) * cellSize.x,
							static_cast<float>(rows) * cellSize.y));

		Core::CursorPosition fitted;
		fitted.Row = static_cast<std::uint16_t>(rows);
		fitted.Column = static_cast<std::uint16_t>(columns);

		return fitted;
	}

	void TerminalView::DrawRow(ImDrawList& drawList, const std::vector<Core::Cell>& cells, int row,
							   const ImVec2& origin, const ImVec2& cellSize, int columns,
							   const EditorPalette& palette)
	{
		const int count = std::min(columns, static_cast<int>(cells.size()));

		if (count <= 0)
		{
			return;
		}

		const float top = origin.y + static_cast<float>(row + m_ScrollOffset) * cellSize.y;
		const float bottom = top + cellSize.y;

		const auto xOf = [&origin, &cellSize](int column) {
			return origin.x + static_cast<float>(column) * cellSize.x;
		};

		std::vector<ImU32> foregrounds(static_cast<std::size_t>(count));
		std::vector<ImU32> backgrounds(static_cast<std::size_t>(count));

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

		if (m_HasSelection)
		{
			for (int column = 0; column < count;)
			{
				if (!IsSelected(row, column))
				{
					++column;
					continue;
				}

				int end = column + 1;

				while (end < count && IsSelected(row, end))
				{
					++end;
				}

				drawList.AddRectFilled(ImVec2(xOf(column), top), ImVec2(xOf(end), bottom),
									   palette.Selection);
				column = end;
			}
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

				AppendUtf8(run, cell.Code);
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

	void TerminalView::HandleMouse(const ImVec2& origin, const ImVec2& cellSize, int visibleRows,
								   int columns)
	{
		const ImVec2 mouse = ImGui::GetMousePos();

		const auto clamped = [this, visibleRows, columns](CellIndex cell) {
			const auto scrollback =
				static_cast<int>(m_Session.GetScreen().GetScrollbackCount());

			cell.Column = std::clamp(cell.Column, 0, std::max(0, columns - 1));
			cell.Row = std::clamp(cell.Row, -scrollback,
								  std::max(0, visibleRows - 1 - m_ScrollOffset));

			return cell;
		};

		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			// A child window nothing focused never sees a key, so without this the first thing
			// typed after a click would go wherever the focus happened to be.
			ImGui::SetWindowFocus();

			const CellIndex cell = clamped(CellUnder(mouse, origin, cellSize));
			const int clicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);

			if (clicks >= 3)
			{
				SelectRowAt(cell, columns);
				m_Sweeping = false;
			}
			else if (clicks == 2)
			{
				SelectWordAt(cell, columns);
				m_Sweeping = false;
			}
			else if (ImGui::GetIO().KeyShift && m_HasSelection)
			{
				// Shift keeps the pressed end where it was, which is how a reader stretches the
				// selection they already made rather than starting another one.
				m_SelectionHead = cell;
				m_Sweeping = true;
			}
			else
			{
				m_SelectionAnchor = cell;
				m_SelectionHead = cell;
				m_HasSelection = false;
				m_Sweeping = true;
			}
		}

		if (!m_Sweeping)
		{
			return;
		}

		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			m_Sweeping = false;
			return;
		}

		m_SelectionHead = clamped(CellUnder(mouse, origin, cellSize));

		// A press that never moved is a click, not a selection of the one cell it landed on.
		m_HasSelection = m_SelectionHead.Row != m_SelectionAnchor.Row ||
						 m_SelectionHead.Column != m_SelectionAnchor.Column;
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
		// nothing for a printable key, so that nothing is sent twice. Skipped while Ctrl or Alt
		// is held: those combinations are control codes, and EncodeKey below is what makes them.
		if (!io.KeyCtrl && !io.KeyAlt)
		{
			std::string typed;

			for (int index = 0; index < io.InputQueueCharacters.Size; ++index)
			{
				AppendUtf8(typed, static_cast<char32_t>(io.InputQueueCharacters[index]));
			}

			if (!typed.empty())
			{
				m_Session.Send(typed);
			}
		}

		const Core::KeyModifier modifiers = ModifiersOf(io);

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

			// The view's own two gestures, judged before the binding lookup below rather than
			// after it. Ctrl+C has to reach the shell as an interrupt when nothing is selected
			// even though the IDE binds it to a copy command, and a paste has to arrive as text
			// on the wire rather than as an editor action.
			if (io.KeyCtrl && !io.KeyShift && *code == Core::KeyCode::C && m_HasSelection)
			{
				if (const std::string selected = CopySelection(); !selected.empty())
				{
					ImGui::SetClipboardText(selected.c_str());
				}

				ClearSelection();
				continue;
			}

			if (io.KeyCtrl && io.KeyShift && *code == Core::KeyCode::V)
			{
				if (const char* clipboard = ImGui::GetClipboardText(); clipboard != nullptr)
				{
					m_Session.Paste(clipboard);
				}

				continue;
			}

			// Anything the IDE has a binding for stays the IDE's. This one rule is what keeps
			// Ctrl+P opening the file finder while Ctrl+A, Ctrl+E, Ctrl+W and Ctrl+R reach the
			// shell's line editing: nothing else distinguishes them, and a terminal that took
			// every key would make the rest of the application unreachable from inside it. The
			// chord is built the way KeyBindingsSection builds one, so the two agree about what
			// a binding is.
			if (!m_Keys.BoundTo(Core::Chord::Key(io.KeyMods | key)).empty())
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
			case Core::TerminalColorKind::Palette: return PaletteColour(color.Index);
			case Core::TerminalColorKind::Rgb: return IM_COL32(color.R, color.G, color.B, 255);
			case Core::TerminalColorKind::Default: break;
		}

		return foreground ? Opaque(palette.Text) : palette.Background;
	}

	void TerminalView::ResolveCell(const Core::Cell& cell, const EditorPalette& palette,
								   ImU32& foreground, ImU32& background) const
	{
		foreground = Resolve(cell.Foreground, palette, true);
		background = Resolve(cell.Background, palette, false);

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

	TerminalView::CellIndex TerminalView::CellUnder(const ImVec2& position, const ImVec2& origin,
													const ImVec2& cellSize) const
	{
		CellIndex cell;

		cell.Column = static_cast<int>(std::floor((position.x - origin.x) / cellSize.x));
		cell.Row = static_cast<int>(std::floor((position.y - origin.y) / cellSize.y)) - m_ScrollOffset;

		return cell;
	}

	const std::vector<Core::Cell>* TerminalView::RowCells(int row, std::vector<Core::Cell>& scratch,
														  int columns) const
	{
		const Core::TerminalScreen& screen = m_Session.GetScreen();

		if (row < 0)
		{
			const int index = static_cast<int>(screen.GetScrollbackCount()) + row;

			if (index < 0)
			{
				return nullptr;
			}

			return &screen.GetScrollbackLine(static_cast<std::size_t>(index));
		}

		if (row >= static_cast<int>(screen.GetRows()))
		{
			return nullptr;
		}

		const int count = std::min(columns, static_cast<int>(screen.GetColumns()));

		scratch.clear();
		scratch.reserve(static_cast<std::size_t>(std::max(0, count)));

		for (int column = 0; column < count; ++column)
		{
			scratch.push_back(screen.At(static_cast<std::uint16_t>(row),
										static_cast<std::uint16_t>(column)));
		}

		return &scratch;
	}

	bool TerminalView::IsSelected(int row, int column) const
	{
		if (!m_HasSelection)
		{
			return false;
		}

		// A rectangle of cells rather than a run that wraps at the right edge: what a terminal
		// puts on a line is a column of a table as often as it is a sentence, and a rectangle is
		// the shape that lets one of those columns be taken on its own.
		const int top = std::min(m_SelectionAnchor.Row, m_SelectionHead.Row);
		const int bottom = std::max(m_SelectionAnchor.Row, m_SelectionHead.Row);
		const int left = std::min(m_SelectionAnchor.Column, m_SelectionHead.Column);
		const int right = std::max(m_SelectionAnchor.Column, m_SelectionHead.Column);

		return row >= top && row <= bottom && column >= left && column <= right;
	}

	void TerminalView::SelectWordAt(const CellIndex& cell, int columns)
	{
		std::vector<Core::Cell> scratch;
		const std::vector<Core::Cell>* cells = RowCells(cell.Row, scratch, columns);

		if (cells == nullptr)
		{
			return;
		}

		const int count = std::min(columns, static_cast<int>(cells->size()));

		if (cell.Column < 0 || cell.Column >= count)
		{
			return;
		}

		const auto isSpace = [cells](int column) {
			return (*cells)[static_cast<std::size_t>(column)].Code == U' ';
		};

		// Bounded by spaces, and a double-click on a gap takes the gap: expanding over cells that
		// match what was clicked means the gesture always selects something, which is what makes
		// a second click on the wrong cell recoverable rather than a no-op.
		const bool space = isSpace(cell.Column);
		int from = cell.Column;
		int to = cell.Column;

		while (from > 0 && isSpace(from - 1) == space)
		{
			--from;
		}

		while (to + 1 < count && isSpace(to + 1) == space)
		{
			++to;
		}

		m_SelectionAnchor = CellIndex{cell.Row, from};
		m_SelectionHead = CellIndex{cell.Row, to};
		m_HasSelection = true;
	}

	void TerminalView::SelectRowAt(const CellIndex& cell, int columns)
	{
		m_SelectionAnchor = CellIndex{cell.Row, 0};
		m_SelectionHead = CellIndex{cell.Row, std::max(0, columns - 1)};
		m_HasSelection = true;
	}

	std::string TerminalView::CopySelection() const
	{
		if (!m_HasSelection)
		{
			return {};
		}

		const int top = std::min(m_SelectionAnchor.Row, m_SelectionHead.Row);
		const int bottom = std::max(m_SelectionAnchor.Row, m_SelectionHead.Row);
		const int left = std::max(0, std::min(m_SelectionAnchor.Column, m_SelectionHead.Column));
		const int right = std::max(m_SelectionAnchor.Column, m_SelectionHead.Column);

		std::string text;
		std::vector<Core::Cell> scratch;

		for (int row = top; row <= bottom; ++row)
		{
			if (row != top)
			{
				text.push_back('\n');
			}

			const std::vector<Core::Cell>* cells = RowCells(row, scratch, right + 1);

			if (cells == nullptr)
			{
				continue;
			}

			const int count = std::min(right + 1, static_cast<int>(cells->size()));
			std::string line;

			for (int column = left; column < count; ++column)
			{
				const Core::Cell& cell = (*cells)[static_cast<std::size_t>(column)];

				// The trailer carries no character of its own; the cell to its left already
				// contributed the whole double-width one.
				if (cell.WideTrailer)
				{
					continue;
				}

				AppendUtf8(line, cell.Code);
			}

			// The grid is space-filled out to its right edge, so without this every copied line
			// would carry a tail of spaces nobody typed — and pasting it back would indent
			// whatever followed.
			while (!line.empty() && line.back() == ' ')
			{
				line.pop_back();
			}

			text.append(line);
		}

		return text;
	}
}
