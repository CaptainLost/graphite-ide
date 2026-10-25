#pragma once

#include <imgui.h>

struct ImGuiWindow;

namespace Graphite {

	// What an interactive tooltip keeps between frames: where it was drawn, for the mouse test and the next
	// frame's placement, and the window that held focus before a click inside it.
	struct InteractiveTooltipState
	{
		ImVec2 Min{};
		ImVec2 Max{};
		float NaturalHeight = 0.0f;
		bool Shown = false;
		ImGuiID FocusBefore = 0;

		bool Contains(ImVec2 point) const;
	};

	struct InteractiveTooltipPlace
	{
		ImGuiViewport* Viewport = nullptr;   // null: the main viewport

		// The editor surface's visible rectangle; empty: the viewport's work area.
		ImVec2 BoundsMin{};
		ImVec2 BoundsMax{};

		ImVec2 AnchorMin{};
		ImVec2 AnchorMax{};

		float LeastHeight = 0.0f;   // kept even when the room on the tooltip's side is less
	};

	struct TooltipBounds
	{
		ImVec2 Min{};
		ImVec2 Max{};
	};

	TooltipBounds InteractiveTooltipBounds(const InteractiveTooltipPlace& place);

	// Opens the tooltip's window, as ADR 0548 describes. False, with nothing to end, while a modal popup is open.
	bool BeginInteractiveTooltip(const char* name, InteractiveTooltipState& state, const InteractiveTooltipPlace& place);

	// Hands focus back after a click inside, raises the window and records where it was drawn.
	void EndInteractiveTooltip(InteractiveTooltipState& state, ImGuiWindow* returnFocusTo);
}
