#pragma once

#include "Editor/DefinitionPreview.h"
#include "Editor/DiagnosticOverlay.h"
#include "Editor/HoverTracker.h"
#include "Editor/LspAnswerRouter.h"

#include "Lang/Server/ClangdHover.h"

#include <imgui.h>

#include <optional>
#include <vector>

namespace Graphite {

	// One document's hover: when to ask and what to show, the answers held, and what the tooltip
	// looked like last frame. Kept per document by DocumentWindows, which fills in the answers;
	// the widget feeds the tracker and draws.
	struct HoverState
	{
		HoverTracker Tracker;

		// The answers held, each with the question it answers. Kept until another token is
		// asked about, which is the cache: the same token at the same revision is not asked again.
		std::optional<HoverQuestion> InfoFor;
		std::optional<HoverInfo> Info;          // nullopt until answered; an empty reply is a
		                                        // HoverInfo with Parsed false and Raw empty
		std::optional<HoverQuestion> PreviewFor;
		std::optional<DefinitionPreview> Preview;

		// The tooltip's rectangle last frame, for "is the mouse inside it".
		ImVec2 TooltipMin{};
		ImVec2 TooltipMax{};
		bool TooltipShown = false;

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
}
