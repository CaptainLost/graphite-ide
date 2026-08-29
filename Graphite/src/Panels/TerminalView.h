#pragma once

#include "Core/Command/Keymap.h"
#include "Core/Term/TerminalSession.h"

#include "Editor/EditorPalette.h"

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
		TerminalView(Core::TerminalSession& session, const Core::Keymap& keys)
			: m_Session(session), m_Keys(keys)
		{
		}

		// Draws into the current window's content region and handles input. Returns the
		// columns and rows that fitted, so the caller can resize the session. The palette
		// carries the editor's colours, which is what the low sixteen ANSI entries map onto
		// so the terminal belongs to the same theme as everything around it.
		Core::CursorPosition Draw(const EditorPalette& palette);

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
		ImU32 Resolve(const Core::TerminalColor& color, const EditorPalette& palette,
					  bool foreground) const;
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
		int m_ScrollOffset = 0;
		bool m_FollowTail = true;
		bool m_HasSelection = false;
		bool m_Sweeping = false;
		CellIndex m_SelectionAnchor;
		CellIndex m_SelectionHead;

		// What the scrollback held last frame. A view scrolled back is pinned to the text the
		// reader is looking at, and every line the shell pushes into the scrollback would
		// otherwise slide that text up by one: the offset is measured from the tail, so it has
		// to grow by as much as the tail moved to leave the content where it was.
		std::size_t m_LastScrollbackCount = 0;
	};
}
