#pragma once

#include "Editor/Overlays/DiagnosticOverlay.h"
#include "Theme/EditorPalette.h"
#include "Editor/Hover/HoverState.h"
#include "Editor/Hover/HoverTracker.h"
#include "Theme/Palettes.h"

#include "Language/Api/LanguageId.h"
#include "Language/Syntax/Token.h"

#include <imgui.h>

#include <cstddef>
#include <vector>

struct ImGuiWindow;

namespace Graphite {

	class PreviewSemantics;
	class SemanticOverlay;

	struct HoverTooltipLook
	{
		EditorPalette Palette;
		SyntaxPalette Syntax;
		float LineHeight = 0.0f;
		float CellWidth = 0.0f;
		std::size_t TabWidth = 4;

		LanguageId Language = LanguageId::PlainText;

		ImGuiViewport* Viewport = nullptr;

		ImVec2 BoundsMin{};
		ImVec2 BoundsMax{};

		const SemanticOverlay* PreviewOverlay = nullptr;
		PreviewSemantics* Semantics = nullptr;
	};

	struct HoverAnchor
	{
		ImVec2 Min{};
		ImVec2 Max{};
	};

	void DrawDiagnosticMessages(const std::vector<DiagnosticSpan>& spans);

	void DrawHoverTooltip(const HoverActions& actions, HoverState& hover,
						  const std::vector<DiagnosticSpan>& diagnostics,
						  const HoverTooltipLook& look, const HoverAnchor& anchor,
						  ImGuiWindow* returnFocusTo);
}
