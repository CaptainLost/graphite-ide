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
#include <string>
#include <string_view>
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

		std::string LoadingText;
	};

	inline constexpr std::string_view kHoverNoDefinition = "No definition found";

	struct HoverSections
	{
		bool Diagnostic = false;
		bool Info = false;
		bool InfoFailure = false;
		bool Preview = false;
		bool NoDefinition = false;
		bool PreviewFailure = false;
		bool Loading = false;

		bool Any() const
		{
			return Diagnostic || Info || InfoFailure || Preview || NoDefinition || PreviewFailure || Loading;
		}
	};

	HoverSections SectionsToShow(const HoverActions& actions, const HoverState& hover,
								 const std::vector<DiagnosticSpan>& diagnostics, const HoverTooltipLook& look);

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
