#pragma once

#include "Editor/DiagnosticOverlay.h"
#include "Editor/EditorPalette.h"
#include "Editor/HoverState.h"
#include "Editor/HoverTracker.h"
#include "Editor/Palettes.h"

#include "Lang/Syntax/Token.h"

#include <imgui.h>

#include <cstddef>
#include <vector>

struct ImGuiWindow;

namespace Graphite {

	// What the tooltip draws with, taken from the editor it belongs to so the declaration and the
	// preview read in the editor's own font, cells and colours.
	struct HoverTooltipLook
	{
		EditorPalette Palette;
		SyntaxPalette Syntax;
		float LineHeight = 0.0f;
		float CellWidth = 0.0f;
		std::size_t TabWidth = 4;

		// The hovered document's lexer, for the declaration line; null draws it in one colour.
		const Lexer* DocumentLexer = nullptr;

		// The editor surface's viewport, which the tooltip is kept inside of and submitted to,
		// so it never spills into a platform window of its own.
		ImGuiViewport* Viewport = nullptr;
	};

	// The screen rectangle of what the tooltip is about: the hovered token, or the cell under the
	// mouse for a diagnostic alone. The tooltip sits flush below it, or flush above.
	struct HoverAnchor
	{
		ImVec2 Min{};
		ImVec2 Max{};
	};

	// Each diagnostic's message, with its source beneath when the server gave one, separated so
	// several overlapping diagnostics read as several. Draws into the current window: the hover
	// tooltip's, or a plain tooltip's for an editor that keeps no hover state.
	void DrawDiagnosticMessages(const std::vector<DiagnosticSpan>& spans);

	// The hover tooltip: one ImGui window, never focused, holding the diagnostic, the symbol's
	// information and its definition preview, as `actions` says. Records its rectangle in
	// `hover` for the next frame's "is the mouse inside it", and clears it when nothing shows.
	//
	// Called from inside the editor surface; `returnFocusTo` is that surface, which keeps the
	// keyboard even when a click lands inside the tooltip.
	void DrawHoverTooltip(const HoverActions& actions, HoverState& hover,
						  const std::vector<DiagnosticSpan>& diagnostics, const HoverTooltipLook& look,
						  const HoverAnchor& anchor, ImGuiWindow* returnFocusTo);
}
