#pragma once

#include "Editor/Navigation/DefinitionPreview.h"
#include "Editor/Overlays/DiagnosticOverlay.h"
#include "Editor/Hover/HoverLayout.h"
#include "Editor/Hover/HoverTracker.h"
#include "Editor/Server/LspAnswerRouter.h"

#include "Language/Api/HoverInfo.h"
#include "Language/Syntax/Token.h"

#include <imgui.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace Graphite {

	struct ColouredPreview
	{
		std::size_t TabWidth = 0;
		DedentedPreview Dedented;
		std::vector<std::vector<Token>> Tokens;
	};

	// One document's hover: when to ask and what to show, the answers held, and what the tooltip
	// looked like last frame. Kept per document by DocumentWindows, which fills in the answers;
	// the widget feeds the tracker and draws.
	struct HoverState
	{
		HoverTracker Tracker;

		// The answers held, each with the question it answers. Kept until another token is
		// asked about, which is the cache: the same token at the same revision is not asked again.
		std::optional<HoverQuestion> InfoFor;
		std::optional<HoverInfo> Info; // nullopt until answered; an empty reply is a
									   // HoverInfo with Parsed false and Raw empty
		std::optional<HoverQuestion> PreviewFor;
		std::optional<DefinitionPreview> Preview;

		// Coloured once, not every frame the tooltip shows: for a grammar that is a parse.
		std::optional<ColouredPreview> PreviewColours;

		// The question whose last answer failed, of each kind. A failure is not held, so it
		// never "arrives"; this is how the tracker learns of it and asks once more. Cleared by
		// the next answer that does not fail, and by the widget when it asks again.
		std::optional<HoverQuestion> InfoFailedFor;
		std::optional<HoverQuestion> PreviewFailedFor;

		// How tall the tooltip's content wanted to be last frame. The window itself may have
		// been held shorter than that, to the room on its side of the token; placing it by that
		// held height would never let it move to the side where it fits.
		float TooltipNaturalHeight = 0.0f;

		// The tooltip's rectangle last frame, for "is the mouse inside it".
		ImVec2 TooltipMin{};
		ImVec2 TooltipMax{};
		bool TooltipShown = false;

		// The window that held the keyboard while the tooltip did not, so a click into the
		// tooltip hands focus back to it rather than to whichever surface the mouse was over.
		// An ID, not a pointer: it is looked up, and so checked, when it is used.
		ImGuiID FocusBeforeTooltip = 0;

		// Where the pointer was last frame, so a frame can tell the mouse moved: a dismissed
		// tooltip comes back only after it does.
		ImVec2 LastMouse{};

		// The diagnostics last found under the mouse. The tracker only remembers that there was
		// one; while it holds the tooltip open for a mouse on its way into it — no longer over
		// the squiggle — these are what the tooltip goes on showing.
		std::vector<DiagnosticSpan> HeldDiagnostics;

		// The screen rectangle the tooltip was last placed against. Kept for a tooltip showing
		// only a diagnostic, which has no token to place against, while the mouse is off the
		// squiggle — otherwise the window would chase the pointer it is trying to receive.
		ImVec2 AnchorMin{};
		ImVec2 AnchorMax{};
	};

	// Holds a hover answer as the answer to `question`. An empty answer is held — the question
	// has been answered, with nothing, and asking again would only say nothing again — but a
	// failed one is not: a server still starting or restarting fails what it is asked, and
	// holding that would leave the token blank until the text changed. What was held stays, and
	// the failure is noted against the question so the tracker can ask it once more.
	void HoldHoverAnswer(HoverState& hover, const HoverQuestion& question,
						 const LanguageServices::Answer& answer);

	// The same for a preview's definition question. An answer naming no definition is held as
	// an unreadable preview; a failed one leaves what was held and is noted as the info's is.
	void HoldPreviewAnswer(HoverState& hover, const HoverQuestion& question,
						   const LanguageServices::Answer& answer,
						   const Core::DocumentSet& documents);

	// Only while hover.Preview holds a value.
	const ColouredPreview& ColouredPreviewOf(HoverState& hover, std::size_t tabWidth);
}
