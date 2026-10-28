#pragma once

#include "Editor/Navigation/DefinitionPreview.h"
#include "Editor/Overlays/DiagnosticOverlay.h"
#include "Editor/Hover/HoverLayout.h"
#include "Editor/Hover/HoverTracker.h"
#include "Editor/Hover/InteractiveTooltip.h"
#include "Editor/Server/LspAnswerRouter.h"

#include "Language/Api/HoverInfo.h"
#include "Language/Syntax/Token.h"

#include <imgui.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct ColouredPreview
	{
		std::size_t TabWidth = 0;
		DedentedPreview Dedented;
		std::vector<std::vector<Token>> Tokens;
	};

	struct HoverState
	{
		HoverTracker Tracker;

		std::optional<HoverQuestion> InfoFor;
		std::optional<HoverInfo> Info;
		std::optional<HoverQuestion> PreviewFor;
		std::optional<DefinitionPreview> Preview;

		std::optional<ColouredPreview> PreviewColours;

		std::optional<HoverQuestion> InfoFailedFor;
		std::optional<HoverQuestion> PreviewFailedFor;

		std::string InfoFailureText;
		std::string PreviewFailureText;

		bool NoDefinitionFound = false;

		InteractiveTooltipState Tooltip;

		ImVec2 LastMouse{};

		std::vector<DiagnosticSpan> HeldDiagnostics;

		ImVec2 AnchorMin{};
		ImVec2 AnchorMax{};
	};

	std::string HoverFailureText(const LanguageServices::Answer& answer);

	std::string HoverLoadingText(const ServerStatus& status);

	void HoldHoverAnswer(HoverState& hover, const HoverQuestion& question,
						 const LanguageServices::Answer& answer);

	void HoldPreviewAnswer(HoverState& hover, const HoverQuestion& question,
						   const LanguageServices::Answer& answer,
						   const Core::DocumentSet& documents);

	const ColouredPreview& ColouredPreviewOf(HoverState& hover, std::size_t tabWidth);
}
