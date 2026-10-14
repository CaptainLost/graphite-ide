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

	class TerminalView
	{
	public:
		TerminalView(Core::TerminalSession& session, const Core::Keymap& keys,
					 const Core::CommandRegistry& commands, Core::EventBus& events)
			: m_Session(session), m_Keys(keys), m_Commands(commands), m_Events(events)
		{
		}

		Core::CursorPosition Draw(const EditorPalette& palette);

		bool DrawContextMenu();

		enum class CopyConfirm
		{
			Silent,
			Toast
		};

		void Copy(CopyConfirm confirm);
		void PasteFromClipboard();
		void Clear();
		void SelectAll();

		bool HasSelection() const { return !m_Selection.IsEmpty(); }
		std::string CopySelection() const;

		void ClearSelection()
		{
			m_Selection.Clear();
			m_Gesture.Cancel();
		}

	private:
		enum class ColourRole
		{
			Foreground,
			Background
		};

		enum class BaseOpacity
		{
			AsResolved,
			ForceOpaque
		};

		static constexpr float kNoScrollbarGrab = -1.0f;

		void DrawRow(ImDrawList& drawList, const std::vector<Core::TerminalCell>& cells, int row,
					 const ImVec2& origin, const ImVec2& cellSize, int columns,
					 const EditorPalette& palette);
		void UpdateSelection(const TerminalGrid& grid);
		void HandleKeyboard();
		void DrawScrollbar(const ImVec2& trackMin, const ImVec2& trackMax, int rows, int scrollback);
		ImU32 Resolve(const Core::TerminalColor& color, const EditorPalette& palette,
					  ColourRole role) const;
		void ResolveCell(const Core::TerminalCell& cell, const EditorPalette& palette, ImU32& foreground,
						 ImU32& background, BaseOpacity baseOpacity = BaseOpacity::AsResolved) const;
		std::size_t SelectionColumns() const;
		bool IsScrollbarGrabbed() const { return m_ScrollbarGrab >= 0.0f; }

		Core::TerminalSession& m_Session;
		const Core::Keymap& m_Keys;
		const Core::CommandRegistry& m_Commands;
		Core::EventBus& m_Events;
		int m_ScrollOffset = 0;
		std::size_t m_Columns = 0;
		float m_ScrollbarGrab = kNoScrollbarGrab;
		bool m_FollowTail = true;
		Core::TextSelection m_Selection;
		Core::SelectionGesture m_Gesture;
		std::uint64_t m_LastRowsScrolledOff = 0;
		bool m_WasAlternate = false;
		std::vector<ImU32> m_RowForegrounds;
		std::vector<ImU32> m_RowBackgrounds;
	};
}
