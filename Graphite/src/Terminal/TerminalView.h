#pragma once

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Terminal/TerminalSession.h"

#include "Theme/EditorPalette.h"

#include <imgui.h>

#include <cstddef>
#include <string>
#include <vector>

namespace Graphite {

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
		TerminalView(Core::TerminalSession& session, const Core::Keymap& keys,
					 const Core::CommandRegistry& commands)
			: m_Session(session), m_Keys(keys), m_Commands(commands)
		{
		}

		// Draws into the current window's content region and handles input. Returns the
		// columns and rows that fitted, so the caller can resize the session. The palette
		// carries the editor's colours, and it is consulted for exactly one thing: a cell
		// whose colour the program left at its default. Everything a program names is drawn
		// in xterm's own values — see the comment above AnsiColour in the .cpp for why the
		// theme is deliberately not consulted there.
		Core::CursorPosition Draw(const EditorPalette& palette);

		// The right-click menu over the grid. Copy, paste and clear are the view's own, because
		// it owns the selection and the screen they act on; closing the session is not, because
		// the session lives in the panel's tab vector. So a chosen Close is returned rather
		// than performed, and the caller does it after the draw is over.
		bool DrawContextMenu();

		// The same three actions the keyboard reaches, named once so the menu and the key are
		// one behaviour rather than two that agree today.
		void Copy();
		void PasteFromClipboard();
		void Clear();

		bool HasSelection() const { return m_HasSelection; }
		std::string CopySelection() const;
		void ClearSelection() { m_HasSelection = false; }

	private:
		struct CellIndex
		{
			// Negative rows address the scrollback, counting back from the top of the screen.
			int Row = 0;
			int Column = 0;
		};

		void DrawRow(ImDrawList& drawList, const std::vector<Core::Cell>& cells, int row,
					 const ImVec2& origin, const ImVec2& cellSize, int columns,
					 const EditorPalette& palette);
		void HandleMouse(const ImVec2& origin, const ImVec2& cellSize, int visibleRows,
						 int columns);
		void HandleKeyboard();

		// Draws the scrollbar in `track` and moves the view with it. Returns whether the mouse
		// is using it, so the grid does not also treat the same press as the start of a
		// selection.
		bool DrawScrollbar(const ImVec2& trackMin, const ImVec2& trackMax, int rows, int scrollback);
		ImU32 Resolve(const Core::TerminalColor& color, const EditorPalette& palette,
					  bool foreground) const;

		// One cell's two colours with the pen's flags already applied. Shared rather than inlined
		// into the row loop because the cursor inverts the cell it sits on, and it has to invert
		// exactly the pair that cell was drawn with.
		// `opaqueBase` forces both resolved colours opaque before the flags are applied, which
		// the cursor needs and a row does not — see the comment beside it in the .cpp.
		void ResolveCell(const Core::Cell& cell, const EditorPalette& palette, ImU32& foreground,
						 ImU32& background, bool opaqueBase = false) const;
		CellIndex CellUnder(const ImVec2& position, const ImVec2& origin,
							const ImVec2& cellSize) const;

		// Fills `scratch` from the screen and returns it, or returns the scrollback line the row
		// names, or nullptr when the row is past either end. The two live in different shapes —
		// the screen answers cell by cell, the scrollback hands back whole lines — and this is
		// the one place that difference is dealt with.
		const std::vector<Core::Cell>* RowCells(int row, std::vector<Core::Cell>& scratch,
												int columns) const;

		bool IsSelected(int row, int column) const;

		void SelectWordAt(const CellIndex& cell, int columns);
		void SelectRowAt(const CellIndex& cell, int columns);

		Core::TerminalSession& m_Session;
		const Core::Keymap& m_Keys;
		const Core::CommandRegistry& m_Commands;
		int m_ScrollOffset = 0;

		// Where in the thumb the scrollbar was grabbed, in pixels from its top, while a drag is
		// under way; negative otherwise.
		float m_ScrollbarGrab = -1.0f;
		bool m_FollowTail = true;
		bool m_HasSelection = false;
		bool m_Sweeping = false;
		CellIndex m_SelectionAnchor;
		CellIndex m_SelectionHead;

		// What the scrollback held last frame. A view scrolled back is pinned to the text the
		// reader is looking at, and every line the shell pushes into the scrollback would
		// otherwise slide that text up by one: the offset is measured from the tail, so it has
		// to grow by as much as the tail moved to leave the content where it was.
		//
		// A growing count is only an approximation of "the tail moved", and it stops being one
		// in two known cases: once the scrollback is at its ten-thousand-line cap the count
		// stays put while lines keep scrolling off the top, and entering or leaving the
		// alternate buffer can make it drop outright. Both leave the pinned view sliding rather
		// than corrupt; a session-side count of lines ever pushed is what would fix it properly.
		std::size_t m_LastScrollbackCount = 0;

		// One row's resolved colours, kept between frames rather than allocated per row. Two
		// vectors a row at sixty frames a second is thousands of allocations a second for
		// buffers that are the same size every time.
		std::vector<ImU32> m_RowForegrounds;
		std::vector<ImU32> m_RowBackgrounds;
	};
}
