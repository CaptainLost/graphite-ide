#pragma once

#include "Core/Text/TextSelection.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace Core {

	enum class SelectionAction
	{
		None,
		Begin,
		Extend,
		SelectWord,
		SelectLine,
		Clear
	};

	struct GestureInput
	{
		int ClickCount = 0; // this frame's press: 0 none, 1, 2, 3+ (ImGui::GetMouseClickedCount)
		bool Shift = false;
		bool OverLine = false; // the press landed on something with a line number
		bool HasSelection = false;
	};

	// What a press does. Drags are handled by the glue (Extend while sweeping).
	SelectionAction DecidePress(const GestureInput& input);

	// Owner callbacks the glue needs, all in the owner's units.
	struct SelectionSource
	{
		// Clamped to the nearest line; nullopt over a non-line.
		std::function<std::optional<TextPoint>(ImVec2 screen)> PointAt;
		// The owner applies WordSpanAt in its own unit.
		std::function<ColumnSpan(TextPoint at)> WordAt;
		std::function<std::size_t(std::int64_t line)> LineLength;
		// For Select All; may be an empty range.
		std::function<std::pair<TextPoint, TextPoint>()> Everything;
		// The owner joins its lines.
		std::function<std::string()> SelectedText;
	};

	struct SelectionGestureResult
	{
		bool Copied = false; // Ctrl+C or the menu's Copy put text on the clipboard
		std::string CopiedText;
	};

	struct SelectionGestureOptions
	{
		// Edge auto-scroll of the current window while sweeping.
		bool ScrollsVertically = true;
		// Handle Ctrl+C here (false where the owner routes Ctrl+C itself, e.g. the terminal).
		bool CopyKey = true;
		// Handle Ctrl+A here (false where the shell owns it).
		bool SelectAllKey = true;
		// Open the shared Copy/Select All context menu under this id; nullptr = the owner has its own.
		const char* MenuId = nullptr;
	};

	class SelectionGesture
	{
	public:
		// Call inside the owner's window/child after its rows are laid out. Handles a press
		// (focusing the window), the drag, and the keys and menu `options` enable.
		SelectionGestureResult Update(TextSelection& selection, const SelectionSource& source,
									  const SelectionGestureOptions& options);

		bool IsSweeping() const
		{
			return m_Sweeping;
		}

		void Cancel()
		{
			m_Sweeping = false;
		}

	private:
		bool m_Sweeping = false;
	};

	// Fills [fromX, toX) of a row at `top` with height `height` in the theme's selection colour
	// (ImGuiCol_TextSelectedBg). Does nothing when toX <= fromX.
	void DrawSelectionSpan(ImDrawList& drawList, float fromX, float toX, float top, float height);
}
