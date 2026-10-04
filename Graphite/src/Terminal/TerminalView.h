#pragma once

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/ImGui/SelectionGesture.h"
#include "Core/Terminal/TerminalSession.h"
#include "Core/Text/TextSelection.h"

#include "Theme/EditorPalette.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Core {

	class EventBus;

}

namespace Graphite {

	struct TerminalGrid;

	// Draws one terminal session and turns mouse and keyboard into bytes for it. It owns the
	// selection and the scroll position, which belong to the view rather than to the session:
	// two views of one session would each have their own.
	class TerminalView
	{
	public:
		// The keymap is taken here rather than per frame because it is one long-lived object
		// owned by the layer, not per-frame data the way the palette is: handing it in sixty
		// times a second would imply a variability it does not have. It is a hard dependency —
		// without it every IDE shortcut typed over this view would be swallowed and sent to the
		// shell instead — so it is a constructor parameter rather than something a caller can
		// forget to set.
		//
		// The registry comes with it because a chord is the IDE's only when the command bound to
		// it can fire while a terminal holds the keyboard: an editor-scoped chord cannot, and
		// the shell gets it.
		//
		// The event bus carries the status bar's confirmation of a copy.
		TerminalView(Core::TerminalSession& session, const Core::Keymap& keys,
					 const Core::CommandRegistry& commands, Core::EventBus& events)
			: m_Session(session), m_Keys(keys), m_Commands(commands), m_Events(events)
		{
		}

		// Draws into the current window's content region and handles input. Returns the
		// columns and rows that fitted, so the caller can resize the session. The palette
		// carries the editor's colours, and it is consulted for exactly one thing: a cell
		// whose colour the program left at its default. Everything a program names is drawn
		// in xterm's own values — see the comment above AnsiColour in the .cpp for why the
		// theme is deliberately not consulted there.
		Core::CursorPosition Draw(const EditorPalette& palette);

		// The right-click menu over the grid. Copy, select all, paste and clear are the view's
		// own, because it owns the selection and the screen they act on; closing the session is
		// not, because the session lives in the panel's tab vector. So a chosen Close is
		// returned rather than performed, and the caller does it after the draw is over.
		bool DrawContextMenu();

		// The same actions the keyboard reaches, named once so the menu and the key are one
		// behaviour rather than two that agree today.
		void Copy();
		void PasteFromClipboard();
		void Clear();

		// From the oldest line of the scrollback to the last screen row that holds anything.
		void SelectAll();

		bool HasSelection() const { return !m_Selection.IsEmpty(); }
		std::string CopySelection() const;

		void ClearSelection()
		{
			m_Selection.Clear();
			m_Gesture.Cancel();
		}

	private:
		void DrawRow(ImDrawList& drawList, const std::vector<Core::Cell>& cells, int row,
					 const ImVec2& origin, const ImVec2& cellSize, int columns,
					 const EditorPalette& palette);

		// The shared gesture over the grid: drag, double-click word, triple-click row and
		// Shift+click. Its keys and its menu stay off - Ctrl+C is settled by HandleKeyboard
		// (ADR 0546), Ctrl+A belongs to the shell, and the view has a menu of its own.
		void UpdateSelection(const TerminalGrid& grid);
		void HandleKeyboard();

		// Draws the scrollbar in `track` and moves the view with it. While the mouse is using it
		// m_ScrollbarGrab is set, which is how the selection gesture knows the press is not its.
		void DrawScrollbar(const ImVec2& trackMin, const ImVec2& trackMax, int rows, int scrollback);
		ImU32 Resolve(const Core::TerminalColor& color, const EditorPalette& palette,
					  bool foreground) const;

		// One cell's two colours with the pen's flags already applied. Shared rather than inlined
		// into the row loop because the cursor inverts the cell it sits on, and it has to invert
		// exactly the pair that cell was drawn with.
		// `opaqueBase` forces both resolved colours opaque before the flags are applied, which
		// the cursor needs and a row does not — see the comment beside it in the .cpp.
		void ResolveCell(const Core::Cell& cell, const EditorPalette& palette, ImU32& foreground,
						 ImU32& background, bool opaqueBase = false) const;

		// The width the selection reads rows at: what the grid fitted last frame, or the screen's
		// own width before the first draw.
		std::size_t SelectionColumns() const;

		Core::TerminalSession& m_Session;
		const Core::Keymap& m_Keys;
		const Core::CommandRegistry& m_Commands;
		Core::EventBus& m_Events;
		int m_ScrollOffset = 0;

		// The columns the grid fitted last frame; zero before the first draw.
		std::size_t m_Columns = 0;

		// Where in the thumb the scrollbar was grabbed, in pixels from its top, while a drag is
		// under way; negative otherwise.
		float m_ScrollbarGrab = -1.0f;
		bool m_FollowTail = true;

		// Lines numbered as TerminalSelection numbers them, so the selection stays on its text
		// while output scrolls it up, and is cleared once that text leaves the scrollback.
		Core::TextSelection m_Selection;
		Core::SelectionGesture m_Gesture;

		// How many rows the screen had pushed into its scrollback last frame. A view scrolled
		// back is pinned to the text the reader is looking at, and every row the shell pushes
		// into the scrollback would otherwise slide that text up by one: the offset is measured
		// from the tail, so it has to grow by as many rows as went in. The screen's count of
		// rows ever pushed keeps moving once the scrollback sits at its cap, where the number of
		// rows it holds stands still.
		std::uint64_t m_LastRowsScrolledOff = 0;

		// Whether the screen showed the alternate buffer last frame; a switch either way clears
		// the selection.
		bool m_WasAlternate = false;

		// One row's resolved colours, kept between frames rather than allocated per row. Two
		// vectors a row at sixty frames a second is thousands of allocations a second for
		// buffers that are the same size every time.
		std::vector<ImU32> m_RowForegrounds;
		std::vector<ImU32> m_RowBackgrounds;
	};
}
