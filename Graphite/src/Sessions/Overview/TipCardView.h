#pragma once

#include "Sessions/Overview/DashboardColours.h"
#include "Sessions/Overview/DashboardTips.h"

namespace Graphite {

	// WARNING: The tooltip a card is drawn in, edged in colours.Frame: it has the tiles' own colour and would merge into
	// them. Pair with ImGui::EndTooltip when it returns true.
	bool BeginTipCard(const DashboardColours& colours);

	// WARNING: Draws inside BeginTipCard/EndTooltip, ending with one Dummy the size of the card.
	void DrawTipCard(const TipCard& card, const DashboardColours& colours);
}
