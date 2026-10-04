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

	struct SelectionSource;

	// The point a press doing `action` acts on, given `boundary` = PointAt at the pointer: the
	// character under the pointer (source.CharacterAt) for a word or a line, when the owner
	// gives one and it answers; `boundary` otherwise.
	std::optional<TextPoint> PressPoint(SelectionAction action, const SelectionSource& source,
										ImVec2 screen, std::optional<TextPoint> boundary);

	// Owner callbacks the glue needs, all in the owner's units.
	struct SelectionSource
	{
		// The boundary between characters nearest to `screen`, clamped to the nearest line;
		// nullopt over a non-line. Where a drag and a single press land.
		std::function<std::optional<TextPoint>(ImVec2 screen)> PointAt;
		// The character under `screen` (its start, truncated rather than rounded), clamped like
		// PointAt. Where a double-click's word and a triple-click's line are looked for, so a
		// double-click on the right half of a word's last character still takes that word
		// rather than the space after it. Optional: unset, or nullopt, falls back to PointAt.
		std::function<std::optional<TextPoint>(ImVec2 screen)> CharacterAt;
		// The owner applies WordSpanAt in its own unit.
		std::function<ColumnSpan(TextPoint at)> WordAt;
		std::function<std::size_t(std::int64_t line)> LineLength;
		// For Select All; may be an empty range.
		std::function<std::pair<TextPoint, TextPoint>()> Everything;
		// The owner joins its lines.
		std::function<std::string()> SelectedText;
		// True when the owner keeps the press at `screen` for itself (a scrollbar it draws
		// without an item, say); the gesture then ignores that press entirely.
		std::function<bool(ImVec2 screen)> Claims;
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
		// Edge auto-scroll sideways by one character width while sweeping.
		bool ScrollsHorizontally = false;
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
		// Call inside the owner's window/child after every widget in it has been submitted.
		// Handles a press (focusing the window), the drag, and the keys and menu `options`
		// enable. A press already taken by an item (ActiveId) or by `source.Claims` is ignored.
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

	// Fills [fromX, toX) of a row at `top` with height `height` in `colour`, or in the theme's
	// selection colour (ImGuiCol_TextSelectedBg) when none is given. Does nothing when
	// toX <= fromX.
	void DrawSelectionSpan(ImDrawList& drawList, float fromX, float toX, float top, float height,
						   std::optional<ImU32> colour = std::nullopt);
}
